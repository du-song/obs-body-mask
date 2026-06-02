// ONNX Runtime session management shared by the body segmenter and pose
// detector. Platform-agnostic: CoreML is requested only on Apple builds and
// always falls back to CPU if the EP cannot be created.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace obm {

enum class Backend { CPU, CoreML };

// Owns an ORT session plus cached input/output names and shapes so callers can
// bind tensors without re-querying every frame.
struct OrtModel {
	std::unique_ptr<Ort::Session> session;
	Ort::MemoryInfo memoryInfo{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

	std::vector<std::string> inputNames;
	std::vector<std::string> outputNames;
	std::vector<std::vector<int64_t>> inputShapes;
	std::vector<std::vector<int64_t>> outputShapes;

	// Convenience views as raw C strings for Session::Run.
	std::vector<const char *> inputNamePtrs() const;
	std::vector<const char *> outputNamePtrs() const;
};

// Creates a session for `modelPath`. Requests CoreML when backend==CoreML on
// Apple platforms, otherwise CPU. On any failure constructing the requested EP
// it retries CPU-only. Returns nullptr (and logs to stderr) if the model
// itself cannot be loaded. `numThreads` applies to the CPU EP.
std::unique_ptr<OrtModel> createOrtModel(Ort::Env &env, const std::string &modelPath,
					 Backend backend, int numThreads = 2);

} // namespace obm
