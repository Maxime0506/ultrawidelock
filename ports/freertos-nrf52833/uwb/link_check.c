/*
 * The entry point of the UWB layer's link proof. It does nothing, on purpose.
 *
 * The work is done by the link options in uwb.cmake, which pull the whole
 * woz_uwb archive in rather than letting --gc-sections discard it. Every symbol
 * the ranging engine, the vendored decadriver and this port's two DW3110
 * backends reference then has to be satisfied by the same libraries the product
 * image links, or this target fails to build.
 *
 * That is a claim the host suite cannot make and the product image does not yet
 * make either: nothing calls into UWB while the application is a skeleton, so
 * the archive is dropped and the image links regardless. Compiling every source
 * is the weaker claim, and the gap between the two is where a call to a vendor
 * function nobody supplies survives.
 */

int main(void)
{
	return 0;
}
