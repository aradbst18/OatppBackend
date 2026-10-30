"""Exercise matrix orchestration without compiling every configuration."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MatrixTests(unittest.TestCase):
    def run_matrix(self, preset="debug", **overrides):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "scripts").mkdir()
            shutil.copy(ROOT / "scripts/test_matrix.sh", root / "scripts")
            tool = root / "cmake"
            tool.write_text("""#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
stage = 'build' if '--build' in args else 'configure'
with open(os.environ['CALLS'], 'a') as out:
    out.write(json.dumps([stage, args]) + '\\n')
negative = any('-neg-' in a or '-verneg-' in a for a in args)
sys.exit(1 if negative or os.environ.get('FAIL_STAGE') == stage else 0)
""")
            tool.chmod(0o755)
            ctest = root / "ctest"
            ctest.write_text("""#!/usr/bin/env python3
import json, os, sys
with open(os.environ['CALLS'], 'a') as out:
    out.write(json.dumps(['test', sys.argv[1:]]) + '\\n')
sys.exit(1 if os.environ.get('FAIL_STAGE') == 'test' else 0)
""")
            ctest.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"],
                       CALLS=str(root / "calls"), MATRIX_BACKENDS="SQLCIPHER",
                       MATRIX_FORMATS="JSON TOML", MATRIX_FEATURES="minimal",
                       MATRIX_VERSION_FORMATS="JSON YAML", MATRIX_HASHES="SHA256 SHA384",
                       MATRIX_PARSERS="0", MATRIX_PG="0", MATRIX_CONFIGURE_ONLY="0",
                       MATRIX_TESTS="1", FAIL_STAGE="")
            env.update(overrides)
            result = subprocess.run(["bash", str(root / "scripts/test_matrix.sh"), preset],
                                    env=env, capture_output=True, text=True)
            calls = [json.loads(line) for line in (root / "calls").read_text().splitlines()]
            return result, calls

    def test_axes_are_distinct_and_preserve_preset(self):
        result, calls = self.run_matrix("relwithdebinfo-asan-ubsan")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        configs = [args for stage, args in calls if stage == "configure"
                   and not any("-neg-" in a for a in args)]
        self.assertEqual(len(configs), 16)  # 2 encryption x 2 config x 2 version x 2 hash
        self.assertEqual(len({a[a.index("-B") + 1] for a in configs}), 16)
        for args in configs:
            self.assertEqual(args[args.index("--preset") + 1], "relwithdebinfo-asan-ubsan")
        self.assertEqual(sum(stage == "test" for stage, _ in calls), 16)

    def test_configure_only_does_not_build_or_test(self):
        result, calls = self.run_matrix(MATRIX_CONFIGURE_ONLY="1")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertTrue(all(stage == "configure" for stage, _ in calls))

    def test_production_presets_are_build_only(self):
        for preset in ("release", "minsizerel"):
            with self.subTest(preset=preset):
                result, calls = self.run_matrix(preset)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertTrue(any(stage == "build" for stage, _ in calls))
                self.assertFalse(any(stage == "test" for stage, _ in calls))

    def test_failures_reach_the_caller(self):
        for stage in ("configure", "build", "test"):
            with self.subTest(stage=stage):
                result, _ = self.run_matrix(FAIL_STAGE=stage)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("FAILED", result.stdout)


if __name__ == "__main__":
    unittest.main()
