// RUN: %dxreflector %s | FileCheck %s

RWByteAddressBuffer output;

[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(32)]
void mainFixed(uint id : SV_DispatchThreadID) {
	output.Store(id * 4, WaveGetLaneCount());
}

[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(16, 64)]
void mainRange(uint id : SV_DispatchThreadID) {
	output.Store(id * 4, WaveGetLaneCount());
}

[shader("compute")]
[numthreads(64, 1, 1)]
[wavesize(16, 64, 32)]
void mainPreferred(uint id : SV_DispatchThreadID) {
	output.Store(id * 4, WaveGetLaneCount());
}

enum EWave { EWave_Small = 16, EWave_Big = 64 };
static const uint PREFERRED = 2 * EWave_Small;

[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(EWave_Small, EWave_Big, PREFERRED)]
void mainExpression(uint id : SV_DispatchThreadID) {
	output.Store(id * 4, WaveGetLaneCount());
}

// CHECK: "Name": "mainFixed"
// CHECK: "Annotations": [
// CHECK-DAG: "[shader(\"compute\")]"
// CHECK-DAG: "[WaveSize(32)]"
// CHECK: "Name": "mainRange"
// CHECK: "Annotations": [
// CHECK-DAG: "[shader(\"compute\")]"
// CHECK-DAG: "[WaveSize(16, 64)]"
// CHECK: "Name": "mainPreferred"
// CHECK: "Annotations": [
// CHECK-DAG: "[shader(\"compute\")]"
// CHECK-DAG: "[WaveSize(16, 64, 32)]"
// CHECK: "Name": "mainExpression"
// CHECK: "Annotations": [
// CHECK-DAG: "[shader(\"compute\")]"
// CHECK-DAG: "[WaveSize(16, 64, 32)]"
