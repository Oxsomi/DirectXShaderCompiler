from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.scm import Git
import os

class dxcompiler(ConanFile):

	name = "dxcompiler"
	version = "2024.06.09"

	# Optional metadata
	license = "LLVM Release License"
	author = "Microsoft (original) & Oxsomi (modifications only)"
	url = "https://github.com/Oxsomi/DirectXShaderCompiler"
	description = "Fork of Microsoft's DXC that contains static linking, conan support and support for other platforms"
	topics = ("hlsl", "dxil", "shader-programs", "directx-shader-compiler")

	# Binary configuration
	settings = "os", "cppstd", "compiler", "build_type", "arch"
	options = { "relaxFloat": [ True, False ], "enableSIMD": [ True, False ] }
	default_options = { "relaxFloat": False, "enableSIMD": True }

	exports_sources = "include/dxc/*"

	def layout(self):
		cmake_layout(self)

	def generate(self):

		deps = CMakeDeps(self)
		deps.generate()

		tc = CMakeToolchain(self)

		tc.cppstd = "20"
		tc.variables["CLANG_BUILD_EXAMPLES"] = False
		tc.variables["CLANG_CL"] = False
		tc.variables["CLANG_INCLUDE_TESTS"] = False
		tc.variables["LLVM_OPTIMIZED_TABLEGEN"] = False
		tc.variables["LLVM_INCLUDE_DOCS"] = False
		tc.variables["LLVM_INCLUDE_EXAMPLES"] = False
		tc.variables["HLSL_INCLUDE_TESTS"] = False
		tc.variables["CLANG_ENABLE_STATIC_ANALYZER"] = False
		tc.variables["CLANG_ENABLE_ARCMT"] = False
		tc.variables["SPIRV_BUILD_TESTS"] = False
		tc.variables["HLSL_BUILD_DXILCONV"] = False
		tc.variables["HLSL_ENABLE_FIXED_VER"] = False
		tc.variables["HLSL_OFFICIAL_BUILD"] = False
		tc.variables["HLSL_OPTIONAL_PROJS_IN_DEFAULT"] = False
		tc.variables["LLVM_ENABLE_TERMINFO"] = False
		tc.variables["BUILD_SHARED_LIBS"] = False

		tc.variables["LLVM_ENABLE_RTTI"] = True
		tc.variables["LLVM_ENABLE_EH"] = True
		tc.variables["LLVM_APPEND_VC_REV"] = True
		tc.variables["LIBCLANG_BUILD_STATIC"] = True
		tc.variables["ENABLE_SPIRV_CODEGEN"] = True
		tc.variables["CMAKE_EXPORT_COMPILE_COMMANDS"] = True
		tc.variables["DXC_USE_LIT"] = True
		tc.variables["LLVM_ENABLE_ASSERTIONS"] = True
		tc.variables["ENABLE_DXC_STATIC_LINKING"] = True

		tc.variables["LLVM_LIT_ARGS"] = "-v"
		tc.variables["LLVM_TARGETS_TO_BUILD"] = "None"
		tc.variables["LLVM_DEFAULT_TARGET_TRIPLE"] = "dxil-ms-dx"

		tc.generate()

	def source(self):
		git = Git(self)
		print(self.conan_data)
		git.clone(url=self.conan_data["sources"][self.version]["url"])
		git.folder = os.path.join(self.source_folder, "DirectXShaderCompiler")
		git.checkout(self.conan_data["sources"][self.version]["checkout"])
		git.run("submodule update --init --recursive")

	def build(self):
		cmake = CMake(self)
		cmake.configure(build_script_folder="DirectXShaderCompiler")
		cmake.build()

	def package(self):
		cmake = CMake(self)
		cmake.build(target="install-distribution")

	def package_info(self):
		self.cpp_info.set_property("cmake_file_name", "dxc")
		self.cpp_info.set_property("cmake_target_name", "dxc")
		self.cpp_info.set_property("pkg_config_name", "dxc")
		self.cpp_info.libs = collect_libs(self)