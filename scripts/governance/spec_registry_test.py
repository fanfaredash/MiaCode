"""Offline negative checks for the CMake spec registry (Python standard library)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "cmake/devtools/MiaCodeSpecRegistry.cmake"


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class SpecRegistryTests(unittest.TestCase):
    def evaluate(self, body, files=("OneSpec.cpp",), check=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src/tools/example").mkdir(parents=True)
            for name in files:
                (root / "src/tools/example" / name).write_text("int main() { return 0; }\n", encoding="utf-8")
            script = root / "check.cmake"
            script.write_text(f'''cmake_minimum_required(VERSION 3.21)
set(CMAKE_SOURCE_DIR "{root.as_posix()}")
function(miacode_add_dev_tool NAME)
    cmake_parse_arguments(DT "TEST" "" "SOURCES;LIBS;INCLUDES" ${{ARGN}})
    set_property(GLOBAL PROPERTY "test_${{NAME}}" "${{DT_TEST}}")
endfunction()
function(set_tests_properties)
endfunction()
include("{REGISTRY.as_posix()}")
{body}
miacode_finalize_spec_registry()
{check or ""}
''', encoding="utf-8")
            return subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)

    @staticmethod
    def declaration(name="one", source="OneSpec.cpp", contract="example.one", extra=""):
        return f'''miacode_add_spec({name}
OWNER src/tools/example CONTRACT {contract} DOMAIN example
KIND boundary RISK high EXECUTION ctest STATUS active PLATFORM all
SOURCES src/tools/example/{source} {extra})\n'''

    def assert_failure(self, result, text):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(text, result.stderr)

    def test_registered_source_and_ctest(self):
        result = self.evaluate(self.declaration(), check='''get_property(test GLOBAL PROPERTY test_one)
if(NOT test)
    message(FATAL_ERROR "CTest was not requested")
endif()''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compile_only_does_not_request_ctest(self):
        body = self.declaration().replace("EXECUTION ctest", "EXECUTION compile-only")
        result = self.evaluate(body, check='''get_property(test GLOBAL PROPERTY test_one)
if(test)
    message(FATAL_ERROR "Compile-only registered CTest")
endif()''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unregistered_source(self):
        self.assert_failure(self.evaluate(self.declaration(), files=("OneSpec.cpp", "TwoSpec.cpp")),
                            "Unregistered: src/tools/example/TwoSpec.cpp")

    def test_missing_source(self):
        self.assert_failure(self.evaluate(self.declaration(source="MissingSpec.cpp")), "missing source")

    def test_duplicate_target(self):
        self.assert_failure(self.evaluate(self.declaration() + self.declaration(source="TwoSpec.cpp", contract="example.two"),
                                          files=("OneSpec.cpp", "TwoSpec.cpp")), "duplicate targets")

    def test_duplicate_source(self):
        self.assert_failure(self.evaluate(self.declaration() + self.declaration(name="two", contract="example.two")),
                            "duplicate sources")

    def test_duplicate_contract(self):
        self.assert_failure(self.evaluate(self.declaration() + self.declaration(name="two", source="TwoSpec.cpp"),
                                          files=("OneSpec.cpp", "TwoSpec.cpp")), "duplicate contracts")

    def test_missing_owner(self):
        self.assert_failure(self.evaluate(self.declaration().replace("OWNER src/tools/example", "OWNER src/missing")),
                            "missing owner")

    def test_blocked_link_cannot_be_ctest(self):
        self.assert_failure(self.evaluate(self.declaration().replace("STATUS active", "STATUS blocked-link")),
                            "blocked-link cannot register CTest")

    def test_catalog_drift(self):
        body = self.declaration() + '''set(MIACODE_SPEC_CATALOG_OUTPUT "${CMAKE_SOURCE_DIR}/catalog.md")
miacode_finalize_spec_registry()
file(APPEND "${MIACODE_SPEC_CATALOG_OUTPUT}" "drift")
set(MIACODE_SPEC_CATALOG_CHECK ON)
'''
        self.assert_failure(self.evaluate(body), "Spec catalog is stale")


if __name__ == "__main__":
    unittest.main()
