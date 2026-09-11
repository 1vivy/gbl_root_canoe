"""CI owns duty selection; test its path decisions without running builds."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('ci_scope', Path(__file__).parents[1] / 'ci_scope.py')
SCOPE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCOPE)


class CiScopeTests(unittest.TestCase):
    def test_paths_select_owning_checks_without_unrelated_firmware_builds(self):
        cases = [
            ('docs/usage.md', set(), set(), set()),
            ('submodules/uefi/tests/test_launch.c', {'uefi_tests'}, set(), {'build-essential', 'e2fsprogs'}),
            ('submodules/uefi/edk2/QcomModulePkg/Application/LinuxLoader/TestBootRoot.c', {'uefi_tests'}, set(), {'build-essential', 'e2fsprogs'}),
            ('submodules/uefi/edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.c', {'uefi_tests', 'firmware', 'versions'}, set(), {'build-essential', 'e2fsprogs'}),
            ('tools/canoe-fs/tests/files.rs', set(), {'canoe-fs'}, {'build-essential'}),
            ('tools/canoe-fs/src/lib.rs', set(), {'canoe-fs', 'canoe-image', 'canoe-bootmgr', 'mode2-profile'}, {'build-essential', 'clang', 'lld'}),
            ('tools/canoe-image/tests/android.rs', set(), {'canoe-image'}, {'build-essential'}),
            ('submodules/patcher/tests/test_libavb_force_success.c', {'patcher_tests'}, set(), {'build-essential'}),
            ('submodules/patcher/src/patchs/core.c', {'patcher_tests'}, {'canoe-image'}, {'build-essential', 'clang', 'lld'}),
            ('targets/magisk_module/module/customize.sh', {'module_tests'}, set(), set()),
            ('scripts/tests/test_imports_check.py', {'script_tests'}, set(), set()),
            ('scripts/firmware_release.py', {'script_tests', 'firmware', 'versions'}, set(), set()),
        ]
        for path, expected_duties, expected_crates, expected_packages in cases:
            with self.subTest(path=path):
                scope = SCOPE.classify([path])
                self.assertEqual(set(scope['apt_packages'].split()), expected_packages)
                self.assertEqual({key for key, value in scope.items() if value is True}, expected_duties)
                self.assertEqual(set(scope['rust_crates'].split()), expected_crates)
                expected_portable = set() if '/tests/' in path else expected_crates.intersection(SCOPE.PORTABLE)
                self.assertEqual(set(scope['portable_crates'].split()), expected_portable)

    def test_explicit_release_or_unclassified_build_inputs_keep_full_qualification(self):
        for scope in (SCOPE.classify([], full=True), SCOPE.classify(['Dockerfile']), SCOPE.classify(['imports.lock.json']), SCOPE.classify(['new-component/source.c'])):
            self.assertTrue(all(value for value in scope.values()))
            self.assertEqual(set(scope['rust_crates'].split()), set(SCOPE.CRATES))
            self.assertEqual(set(scope['portable_crates'].split()), set(SCOPE.PORTABLE))
            self.assertEqual(set(scope['apt_packages'].split()), {'build-essential', 'clang', 'lld', 'dosfstools', 'e2fsprogs'})
