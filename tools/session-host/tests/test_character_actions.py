"""Exercise the production character quick-action controller at HWND boundaries."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def method(text, signature):
    start = text.index(signature)
    at = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at] + '\n'


STUBS = r'''
#include "fssessionexperience.h"
#include "fssessionlifecycle.h"
#include <cassert>
#include <memory>
#include <string>
using namespace fs_session;
using HWND=int;
constexpr int CB_SETCURSEL=1,SW_RESTORE=2;
int selectedControl=-1,foreground=0,restored=0;
bool IsIconic(HWND){return true;}
void ShowWindow(HWND window,int command){assert(command==SW_RESTORE);restored=window;}
void SetForegroundWindow(HWND window){foreground=window;}
void SendMessageW(HWND,int command,int index,int){assert(command==CB_SETCURSEL);selectedControl=index;}
struct Pipe{bool live=true;bool alive()const{return live;}};
struct Recovery{bool recovering=false;bool active()const{return recovering;}};
struct RestartState{Restart::Phase value=Restart::Phase::Idle;Restart::Phase phase()const{return value;}};
struct Slot{
 bool live=true,detached=false;Message snapshot;Pipe pipe;Recovery recovery;RestartState restart;HWND handle=42;
 bool running()const{return live;}HWND surface()const{return handle;}
};
struct Host{
 std::unique_ptr<Slot> slots[MaxCharacters];HostLifecycle lifecycle;HWND characterControl=1;
 bool closing=false,blocked=false;int active=0,launched=-1,activated=-1,launches=0,layouts=0;std::wstring notice;
 bool busy()const{return blocked;}
 std::wstring profileName(int i)const{return i==1?L"Sodie":L"Character "+std::to_wstring(i+1);}
 std::wstring characterName(int i)const{return profileName(i);}
 void launch(int i){launched=i;++launches;slots[i]=std::make_unique<Slot>();slots[i]->snapshot.state=State::Login;}
 void switchTo(int i){activated=i;active=i;}
 void message(const std::wstring& s){notice=s;}
 void layout(){++layouts;}
'''

CASES = r'''
};
int main(){
 Host h;
 assert(h.quickLabel(CharacterQuickBase+1)==L"Sodie");
 assert(h.quickChoiceLabel(CharacterQuickBase+1)==L"Character 2 · Sodie");
 assert(h.quickChoiceLabel(CharacterQuickBase)==L"Character 1");
 assert(h.quickLabel(274)==L"Attention");
 h.openCharacter(4);assert(h.launched==4 && h.launches==1 && h.lifecycle.selection()==4 && selectedControl==4);
 assert(h.characterQuickTip(4)==L"Show Character 5 login");
 h.openCharacter(4);assert(h.launches==1 && foreground==42 && restored==42); // Repeated click reuses the login.
 h.slots[1]=std::make_unique<Slot>();h.slots[1]->snapshot.state=State::Ready;
 h.openCharacter(1);assert(h.activated==1 && h.active==1 && h.lifecycle.selection()==1 && selectedControl==1 && h.launches==1);
 assert(h.characterQuickTip(1)==L"Sodie · active");
 h.activated=-1;h.blocked=true;h.openCharacter(2);assert(h.launches==1 && h.lifecycle.selection()==1);
 h.blocked=false;h.closing=true;h.openCharacter(2);assert(h.launches==1);h.closing=false;
 h.slots[4]->live=false;h.slots[4]->restart.value=static_cast<Restart::Phase>(1);
 h.openCharacter(4);assert(h.launches==1 && h.lifecycle.selection()==1); // Pending restart cannot spawn a second login.
 h.slots[4]->restart.value=Restart::Phase::Idle;h.openCharacter(4);assert(h.launches==2 && h.launched==4);
 for(int state=0;state<4;++state){
  auto& slot=*h.slots[1];slot.detached=state==0;slot.recovery.recovering=state==1;slot.pipe.live=state!=2;
  slot.snapshot.state=state==3?State::Disconnected:State::Ready;foreground=0;h.openCharacter(1);
  assert(foreground==42 && h.activated==-1 && h.launches==2); // A live detached/recovering/disconnected viewer is reused.
 }
 h.slots[1]->snapshot.state=State::Login;h.slots[1]->handle=0;h.openCharacter(1);assert(h.launches==2 && !h.notice.empty());
 h.openCharacter(-1);h.openCharacter(MaxCharacters);assert(h.launches==2); // Invalid slot cannot change selection or start a process.
 for(bool running:{false,true})for(bool ready:{false,true})for(bool managed:{false,true}){
  assert(characterQuickAction(running,ready,managed,true)==CharacterQuickAction::Unavailable);
  const auto action=characterQuickAction(running,ready,managed,false);
  assert(action==(running?(ready&&managed?CharacterQuickAction::Activate:CharacterQuickAction::ShowViewer):CharacterQuickAction::OpenLogin));
 }
}
'''


class CharacterActions(unittest.TestCase):
    def test_production_controller(self):
        text = (ROOT / 'tools/session-host/main.cpp').read_text()
        methods = ''.join(method(text, signature) for signature in (
            '    std::wstring quickLabel(', '    std::wstring quickChoiceLabel(',
            '    CharacterQuickAction characterAction(', '    std::wstring characterQuickTip(',
            '    void openCharacter('))
        with tempfile.TemporaryDirectory(prefix='fs-character-actions-') as tmp:
            source, exe = Path(tmp) / 'check.cpp', Path(tmp) / 'check'
            source.write_text(STUBS + methods + CASES)
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-UNDEBUG', '-I',
                str(ROOT / 'indra/newview'), str(source), '-o', str(exe)], check=True, timeout=45)
            subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
