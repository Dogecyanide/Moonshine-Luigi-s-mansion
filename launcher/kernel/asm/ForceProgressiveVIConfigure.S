#include <asm.h>

# r3: GXRenderModeObj*
# Runs after VIConfigure has saved its caller state.

	li	%r0, 2
	stw	%r0, 0x00(%r3)	# VI_NTSC progressive
	li	%r0, 0
	stw	%r0, 0x14(%r3)	# VI_XFBMODE_SF
	stb	%r0, 0x18(%r3)	# no field rendering
	mr	%r31, %r3		# displaced instruction
	blr
