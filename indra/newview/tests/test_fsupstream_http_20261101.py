"""Production cooperative teardown order; does not model stuck native workers."""
from pathlib import Path
import os,shlex,subprocess,tempfile,unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[2]
class HTTPShutdownTests(unittest.TestCase):
 def test_stats_outlive_service_and_cleanup_is_idempotent(self):
  text=(ROOT/'llcorehttp/httprequest.cpp').read_text()
  source=r'''
#include <cassert>
#include <vector>
bool has_inited=true,statsAlive=true,workerAlive=true;std::vector<int> order;
struct HttpStatus{};
struct HttpService{static void term(){assert(statsAlive);workerAlive=false;order.push_back(1);}};
struct HTTPStats{static void deleteSingleton(){assert(!workerAlive);statsAlive=false;order.push_back(2);}};
struct HttpRequestQueue{static void term(){assert(!workerAlive&&!statsAlive);order.push_back(3);}};
struct HttpRequest{static HttpStatus destroyService();};
'''+function(text,'HttpStatus HttpRequest::destroyService()')+r'''
int main(){HttpRequest::destroyService();assert((order==std::vector<int>{1,2,3}));assert(!has_inited);HttpRequest::destroyService();assert(order.size()==3);}
'''
  with tempfile.TemporaryDirectory(prefix='fs-upstream-http-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(source);exe=p.with_suffix('')
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
