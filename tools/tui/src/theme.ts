import { RGBA } from "@opentui/core"

// Use the terminal's own default foreground, background, and ANSI palette.
// This deliberately avoids imposing an application colour scheme on the user.
export const theme = {
  foreground: RGBA.defaultForeground(),
  background: RGBA.defaultBackground(),
  line: RGBA.fromIndex(8),
  muted: RGBA.fromIndex(8),
  nrf: RGBA.defaultForeground(),
  esp32: RGBA.defaultForeground(),
  success: RGBA.fromIndex(2),
  warning: RGBA.fromIndex(3),
  danger: RGBA.fromIndex(1)
} as const

export const severityColor = {
  info: theme.foreground,
  success: theme.success,
  warning: theme.warning,
  error: theme.danger
} as const
