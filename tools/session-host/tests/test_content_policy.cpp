/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fscontentpolicy.h"
#include <cassert>
#include <iostream>
int main()
{
 using namespace fs_content;
 assert(utf8("hello\n😀"));assert(!utf8(std::string("a\0b",3)));assert(!utf8("\xc0\x80"));assert(!utf8("\xed\xa0\x80"));assert(!utf8("\xf4\x90\x80\x80"));assert(!utf8("\xe2\x82"));
 const std::string head="Linden text version 2\n{\nLLEmbeddedItems version 1\n{\ncount 0\n}\n";
 const auto raw=head+"Text length 3\nabc}\n";std::string prefix,suffix;
 assert(notecardLayout(raw,"abc",raw.size()-2,prefix,suffix));assert(prefix==head && suffix=="}\n");
 assert(replaceNotecard(prefix,suffix,"new text")==head+"Text length 8\nnew text}\n");
 assert(!notecardLayout(raw+"junk","abc",raw.size()-2,prefix,suffix));assert(!notecardLayout(raw,"abc",raw.size()-3,prefix,suffix));
 auto malformed=raw;malformed[malformed.find("length 3")+7]='4';assert(!notecardLayout(malformed,"abc",raw.size()-2,prefix,suffix));
 assert(!notecardLayout(raw,"ab",raw.size()-2,prefix,suffix));
 const std::string item="\xf4\x80\x80\x80";assert(validEmbeddedReferences(item,1));assert(!validEmbeddedReferences(item,0));assert(preserveEmbedded("a"+item,"b"+item));assert(!preserveEmbedded("a"+item,"b"));assert(!preserveEmbedded("a",item));
 auto change=diff("hello 😀 world","hello 😁 world");assert(utf8(change.before) && utf8(change.after));assert(change.before=="😀" && change.after=="😁");
 auto bounded=diff(std::string(10000,'a'),std::string(10000,'b'));assert(bounded.truncated && bounded.before.size()+bounded.after.size()<=4096);
 assert(excerpt("😀",3).empty());assert(diff("abc","abc").removed==0);
 assert(sameSourceEpoch("source-one","source-one"));assert(!sameSourceEpoch("source-one","source-two"));assert(!sameSourceEpoch("",""));
 assert(canInvalidateHiddenSource(true,false,true,false));
 assert(!canInvalidateHiddenSource(true,true,true,false));assert(!canInvalidateHiddenSource(true,false,false,false));
 assert(!canInvalidateHiddenSource(true,false,true,true));assert(!canInvalidateHiddenSource(false,false,true,false));
 auto location=diagnosticLocation("(3, 15) : ERROR : syntax");assert(location.known && location.line==3 && location.column==15);
 assert(!diagnosticLocation("(999999999999999, 2) error").known);assert(!diagnosticLocation("(2, -1) error").known);assert(!diagnosticLocation("unknown error").known);
 std::cout<<"content policy checks passed\n";
}
