/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../fsassistantsocialmodel.h"
#include <cassert>
#include <string>
int main()
{
    using namespace fs_assistant_social;
    Stream<std::string> stream;
    for(int i=0;i<520;++i)stream.append(std::to_string(i),1);
    assert(stream.entries().size()==512 && stream.first()==9 && stream.latest()==520);
    assert(stream.gap(0) && !stream.gap(8));
    auto readerA=stream.first()-1,readerB=readerA;
    for(const auto& entry:stream.entries())if(entry.id>readerA){readerA=entry.id;break;}
    assert(readerA==9 && readerB==8); // independent readers; no destructive pop
    stream.clear();assert(stream.latest()==520 && stream.first()==521 && stream.gap(readerB));
    assert(stream.append("new authority",4)==521);
    stream.append("oversized",Stream<std::string>::ByteCapacity+1);assert(stream.entries().empty() && stream.latest()==522);
    Draft draft;assert(setDraft(draft,0,"typing"));assert(!setDraft(draft,0,"overwrite"));assert(draft.text=="typing" && draft.revision==1);
    assert(!liveMessage(true,1,true) && !liveMessage(true,2,true) && !liveMessage(false,0,false) && liveMessage(false,0,true));
    assert(validText("hello") && !validText("") && !validText(std::string(1024,'x')) && !validText(std::string("x\0y",3)));
}
