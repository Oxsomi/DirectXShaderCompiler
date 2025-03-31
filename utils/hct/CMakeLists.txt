# generate hlsl_intrinsic_opcodes.json to preserve high level intrinsic opcodes
# This uses CODE_TAG because the file exists in the source tree.

include(HCT)
add_hlsl_hctgen(HlslIntrinsicOpcodes OUTPUT hlsl_intrinsic_opcodes.json CODE_TAG)
