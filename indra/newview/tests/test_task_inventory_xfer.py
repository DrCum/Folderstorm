"""Exercise legacy task-inventory replies and EOF callbacks without a simulator."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

from test_fsobjecttools_native import ROOT, function


class TaskInventoryTransfer(unittest.TestCase):
    def test_retry_and_superseded_transfers(self):
        viewer = (ROOT / 'llviewerobject.cpp').read_text(encoding='utf-8')
        context = viewer[viewer.index('struct LLFilenameAndTask'):viewer.index('// static\nvoid LLViewerObject::processTaskInv(')]
        methods = [
            'bool LLViewerObject::isInventoryPending()',
            'void LLViewerObject::requestInventory()',
            'void LLViewerObject::fetchInventoryFromServer()',
            'void LLViewerObject::processTaskInv(LLMessageSystem* msg, void** user_data)',
            'void LLViewerObject::processTaskInvFile(void** user_data, S32 error_code, LLExtStat ext_status)',
            'void LLViewerObject::doInventoryCallback(bool from_simulator, bool complete)',
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'callbacks.inc').write_text(context + ''.join(function(viewer, m) for m in methods))
            cpp = ROOT / 'tests' / 'task_inventory_xfer_fixture.cpp'
            if os.name == 'nt' and Path(os.environ.get('CXX', 'cl')).stem.lower() == 'cl':
                exe = path / 'check.exe'
                command = [os.environ.get('CXX', 'cl'), '/nologo', '/EHsc', '/std:c++17',
                           '/I' + directory, str(cpp), '/Fe:' + str(exe)]
            else:
                exe = path / 'check'
                command = shlex.split(os.environ.get('CXX', 'c++')) + [
                    '-std=c++17', '-I' + directory, str(cpp), '-o', str(exe)]
            subprocess.run(command, cwd=directory, check=True)
            subprocess.run([exe], cwd=directory, check=True)


if __name__ == '__main__':
    unittest.main()
