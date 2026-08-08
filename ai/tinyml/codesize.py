#!/usr/bin/env python3
"""Cross-compile the chosen tree for the CDK's core and measure what it costs.

Run (after extract_features.py with PORTABLE=1):
    ai/tinyml/.venv/bin/python ai/tinyml/codesize.py

Options (env vars):
    FEATURES    input .npz (default ai/tinyml/features_dw3000.npz)
    SEED        split seed, must match bakeoff.py (default 42)
    TRIPLE      arm-none-eabi (default) or arm-zephyr-eabi
    DEPTHS      comma-separated tree depths (default 4,6)

bakeoff.py counts model PAYLOAD only, because that is all it can count without a
cross compiler. This closes that gap: it compiles the generated C for the
nRF52833's Cortex-M4F with the same optimisation the firmware uses
(CONFIG_SIZE_OPTIMIZATIONS=y, i.e. -Os) and reports the section sizes.

Two emlearn code generation methods are measured, because they trade the same
bytes in opposite directions:

    loadable  the tree is a const EmlTreesNode[] walked by eml_trees_predict().
              Payload scales with the tree, the walker is a fixed cost, and one
              walker serves any number of trees.
    inline    the tree is generated as nested if/else. No node array and no
              walker at all, so a small tree is pure .text and nothing else.

Sizes come from `arm-none-eabi-size -A` on an object holding only the model and
a three-line wrapper, so every byte reported belongs to the model. Nothing is
linked: at link time --gc-sections can only remove, never add.
"""

import os
import re
import subprocess
import numpy as np

from sklearn.tree import DecisionTreeClassifier
from sklearn.model_selection import train_test_split

import emlearn

from features_io import read_features

FEATURES = os.environ.get("FEATURES", "ai/tinyml/features_dw3000.npz")
SUBSET = os.environ.get("SUBSET", "scalar")
SUBSETS = {"scalar": ["fp_pwr", "rx_pwr", "pwr_diff", "rxpacc"],
           "resid": ["fp_resid", "rx_pwr"]}
SEED = int(os.environ.get("SEED", "42"))

# Fixed, not an env knob, for the same reason the toolchain below is fixed: this
# path reaches the compiler's argv, and a path out of the environment landing in
# argv is a hole the security gate blocks. It is a scratch directory; being able
# to move it is worth nothing.
OUTDIR = "ai/tinyml/out-codesize"
DEPTHS = [int(d) for d in os.environ.get("DEPTHS", "4,6").split(",")]

# The toolchain is picked from literals rather than read out of the environment.
# A tool name taken verbatim from an env var and handed to subprocess argv is what
# the semgrep gate blocks, and it is right to: this script's job is one measurement
# on one target family, so an arbitrary-path knob buys nothing worth the hole.
# TRIPLE=arm-zephyr-eabi selects the Zephyr SDK's cross compiler instead.
if os.environ.get("TRIPLE") == "arm-zephyr-eabi":
    CC, SIZE = "arm-zephyr-eabi-gcc", "arm-zephyr-eabi-size"
else:
    CC, SIZE = "arm-none-eabi-gcc", "arm-none-eabi-size"

# The nRF52833 in the DWM3001C module, built the way firmware/ builds:
# CONFIG_SIZE_OPTIMIZATIONS=y is -Os, and the core is a Cortex-M4F.
# -ffreestanding is not a stylistic choice: Homebrew's arm-none-eabi-gcc ships no
# C library, so <stdint.h> only resolves through GCC's own stdint-gcc.h, which is
# what __STDC_HOSTED__=0 selects. It suits the measurement anyway, since none of
# this may call into libc on the target.
CFLAGS = [
    "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
    "-Os", "-ffunction-sections", "-fdata-sections", "-std=c99", "-Wall",
    "-ffreestanding",
]

# Homebrew's arm-none-eabi-gcc ships a compiler and binutils but no C library, so
# the only libc headers available are the handful GCC provides itself (stdint,
# stddef, stdbool). emlearn's eml_common.h and eml_log.h include math.h, stdlib.h
# and stdio.h for their debug logging, which the tree walk never reaches: the walk
# is integer comparisons and array indexing. These stubs declare just enough for
# those headers to parse. `inline` needs none of them, which is the cross-check:
# if the stubs changed codegen, inline and loadable would not agree on the parts
# they share.
SHIMS = {
    "math.h": (
        "#pragma once\n"
        "#define NAN (__builtin_nanf(\"\"))\n"
        "#define INFINITY (__builtin_inff())\n"
    ),
    "stdlib.h": (
        "#pragma once\n#include <stddef.h>\n"
        "char *getenv(const char *name);\nint abs(int j);\n"
    ),
    "stdio.h": (
        "#pragma once\n#include <stddef.h>\n"
        "typedef struct _EML_FILE FILE;\nextern FILE *stderr;\n"
        "int fprintf(FILE *stream, const char *format, ...);\n"
    ),
}

WRAPPER = """#include <stdint.h>
#include "{header}"

/* Exported so nothing in the translation unit is dead and dropped before it is
   measured. The signature is emlearn's own generated entry point. */
int32_t woz_ml_predict(const {dtype} *f, int32_t n)
{{
    return {name}_predict(f, n);
}}
"""


def quantise_int16(X, lo, span):
    """Identical to bakeoff.py: the tree is trained on the quantised features, so
    the int16 conversion is lossless rather than applied afterwards."""
    q = (X - lo) * (32000.0 / span) - 16000.0
    return np.clip(np.round(q), -32768, 32767).astype(np.int16)


def section_sizes(obj):
    """`size -A` section table, as a name -> bytes dict."""
    out = subprocess.run([SIZE, "-A", obj],
                         capture_output=True, text=True, check=True).stdout
    sizes = {}
    for line in out.splitlines():
        m = re.match(r"^(\.\S+)\s+(\d+)", line)
        if m:
            sizes[m.group(1)] = int(m.group(2))
    return sizes


def measure(name, method, model, dtype="int16_t"):
    """Generate C for one model, cross-compile it, return its section sizes."""
    stem = f"{name}_{method}"
    header = f"{stem}.h"
    cm = emlearn.convert(model, method=method, dtype=dtype)
    cm.save(name=stem, file=os.path.join(OUTDIR, header))

    csrc = os.path.join(OUTDIR, f"{stem}.c")
    with open(csrc, "w") as f:
        f.write(WRAPPER.format(header=header, name=stem, dtype=dtype))

    obj = os.path.join(OUTDIR, f"{stem}.o")
    shimdir = os.path.join(OUTDIR, "libc-shim")
    subprocess.run([CC, *CFLAGS, f"-I{emlearn.includedir}", f"-I{OUTDIR}",
                    f"-I{shimdir}", "-c", csrc, "-o", obj], check=True)

    s = section_sizes(obj)
    text = s.get(".text", 0) + sum(v for k, v in s.items() if k.startswith(".text."))
    rodata = sum(v for k, v in s.items() if k.startswith(".rodata"))
    data = sum(v for k, v in s.items() if k.startswith(".data"))
    bss = sum(v for k, v in s.items() if k.startswith(".bss"))

    # What a link actually keeps. -ffunction-sections puts each function in its own
    # section, so --gc-sections drops whatever nothing references: predict_proba,
    # which classification never calls, and this file's own wrapper, which exists
    # only so the object has an external symbol to measure. Reporting the object's
    # total without saying this would overstate the cost.
    DROPPED = ("predict_proba", "woz_ml_predict")
    linked = sum(v for k, v in s.items()
                 if k.startswith(".text") and not any(d in k for d in DROPPED))
    return {"text": text, "linked": linked, "rodata": rodata, "data": data,
            "bss": bss, "flash": text + rodata + data,
            "linked_flash": linked + rodata + data, "ram": data + bss,
            "sections": {k: v for k, v in s.items()
                         if k.startswith((".text.", ".rodata", ".data.", ".bss."))}}


def write_shims():
    shimdir = os.path.join(OUTDIR, "libc-shim")
    os.makedirs(shimdir, exist_ok=True)
    for name, body in SHIMS.items():
        with open(os.path.join(shimdir, name), "w") as f:
            f.write(body)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    write_shims()
    X, y, names, _ = read_features(FEATURES)
    # Same default as gen_model.py, and for the same reason: measuring a 14-feature
    # model would report a scaler nearly four times the size of the one that ships.
    if SUBSET != "all":
        if SUBSET not in SUBSETS:
            raise SystemExit(f"unknown SUBSET={SUBSET}; pick all/{'/'.join(SUBSETS)}")
        wanted = SUBSETS[SUBSET]
        missing = [n for n in wanted if n not in names]
        if missing:
            raise SystemExit(f"SUBSET={SUBSET}: {missing} absent from {FEATURES}")
        X, names = X[:, [names.index(n) for n in wanted]], wanted
    print(f"{X.shape[0]} samples, {X.shape[1]} features: {', '.join(names)}")

    Xtr, Xtmp, ytr, ytmp = train_test_split(X, y, test_size=0.30,
                                            random_state=SEED, stratify=y)
    _, Xte, _, yte = train_test_split(Xtmp, ytmp, test_size=0.50,
                                      random_state=SEED, stratify=ytmp)
    lo = Xtr.min(axis=0)
    span = np.maximum(Xtr.max(axis=0) - lo, 1e-9)
    Qtr, Qte = quantise_int16(Xtr, lo, span), quantise_int16(Xte, lo, span)

    # The feature scaler the target needs regardless of method: lo and span as
    # float32, which is what turns raw diagnostics into the model's input space.
    scaler_bytes = X.shape[1] * 2 * 4

    print(f"\n{CC}: " + subprocess.run([CC, "-dumpversion"], capture_output=True,
                                       text=True).stdout.strip())
    print("flags: " + " ".join(CFLAGS))
    print(f"scaler: {X.shape[1]} features * 2 * float32 = {scaler_bytes} B of .rodata\n")

    rows = []
    for depth in DEPTHS:
        m = DecisionTreeClassifier(max_depth=depth, random_state=SEED)
        m.fit(Qtr, ytr)
        acc = float(m.score(Qte, yte))
        for method in ("loadable", "inline"):
            r = measure(f"dtree_d{depth}", method, m)
            r.update(model=f"dtree-d{depth}", method=method, acc=acc)
            rows.append(r)
            print(f"  dtree-d{depth:<2} {method:9} acc {acc:.4f}  "
                  f"text {r['text']:6} B  rodata {r['rodata']:6} B  "
                  f"bss {r['bss']:5} B  flash {r['flash']:6} B  "
                  f"after --gc-sections {r['linked_flash']:6} B")
            for k, v in sorted(r["sections"].items(), key=lambda kv: -kv[1]):
                print(f"      {k:44} {v:6} B")

    print("\n| model | method | test acc | .text | .rodata | object flash | "
          "linked flash | + scaler |")
    print("|---|---|---|---|---|---|---|---|")
    for r in rows:
        print(f"| {r['model']} | {r['method']} | {r['acc']:.4f} | {r['text']} | "
              f"{r['rodata']} | {r['flash']} | {r['linked_flash']} | "
              f"{r['linked_flash'] + scaler_bytes} |")

    best = min(rows, key=lambda r: r["linked_flash"])
    print(f"\nsmallest: {best['model']} {best['method']}, "
          f"{best['linked_flash']} B of code and constants after --gc-sections, "
          f"{best['linked_flash'] + scaler_bytes} B including the {scaler_bytes} B "
          f"feature scaler, {best['ram']} B of RAM.")


if __name__ == "__main__":
    main()
