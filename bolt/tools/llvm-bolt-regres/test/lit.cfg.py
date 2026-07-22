import os
import lit.formats

config.name = 'llvm-bolt-regres'
config.test_format = lit.formats.ShTest(False)
config.suffixes = ['.s', '.test']
config.test_source_root = os.path.dirname(__file__)

# Add build/bin (where llvm-mc, FileCheck, and llvm-bolt-regres live) to PATH
bindir = os.path.abspath(os.path.join(config.test_source_root, '../../../../build/bin'))
config.environment['PATH'] = bindir + os.path.pathsep + os.environ.get('PATH', '')
