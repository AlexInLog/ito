from conan import ConanFile
from conan.tools.cmake import cmake_layout


class ItoConan(ConanFile):
    name = "ito"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain", "CMakeConfigDeps"

    options = {
        "with_tests": [True, False],
        "with_benchmarks": [True, False],
    }
    default_options = {
        "with_tests": False,
        "with_benchmarks": False,
    }

    def requirements(self):
        if self.options.with_tests or self.options.with_benchmarks:
            self.requires("catch2/3.15.1")
        if self.options.with_tests:
            self.requires("trompeloeil/49")
 
    def layout(self):
        cmake_layout(self)
        
