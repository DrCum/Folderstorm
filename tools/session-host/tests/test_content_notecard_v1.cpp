/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fscontentpolicy.h"
#include <cassert>
#include <iostream>
int main()
{
 using namespace fs_content;
 // Complete native version 1 serialization, including its closing structure.
 const std::string plain="Linden text version 1\n{\nLLEmbeddedItems version 1\n{\ncount 0\n}\nText length 12\nLegacy note!}\n";
 std::string prefix,suffix,text;
 assert(notecardLayout(plain,"Legacy note!",plain.size()-2,prefix,suffix,1));
 assert(notecardReadText("Legacy note!",1,0,text) && text=="Legacy note!");
 assert(!notecardLayout(plain,"Legacy note!",plain.size()-2,prefix,suffix)); // v2 write gate remains explicit
 // Embedded v1 inventory uses native ASCII index byte 0x80, not invalid UTF-8.
 const std::string embeddedPrefix="Linden text version 1\n{\nLLEmbeddedItems version 1\n{\ncount 1\n{\next char index 0\ninv_item\t0\n{\n"
  "item_id\t11111111-1111-1111-1111-111111111111\nparent_id\t00000000-0000-0000-0000-000000000000\n"
  "permissions 0\n{\nbase_mask\t7fffffff\nowner_mask\t7fffffff\ngroup_mask\t00000000\neveryone_mask\t00000000\nnext_owner_mask\t7fffffff\n"
  "creator_id\t22222222-2222-2222-2222-222222222222\nowner_id\t22222222-2222-2222-2222-222222222222\nlast_owner_id\t22222222-2222-2222-2222-222222222222\ngroup_id\t00000000-0000-0000-0000-000000000000\n}\n"
  "asset_id\t33333333-3333-3333-3333-333333333333\ntype\tnotecard\ninv_type\tnotecard\nflags\t00000000\nsale_info\t0\n{\nsale_type\tnot\nsale_price\t0\n}\nname\tEmbedded note|\ndesc\t|\ncreation_date\t0\n}\n}\n}\n";
 const std::string legacyText=std::string("a")+char(0x80)+"b";
 const auto embedded=embeddedPrefix+"Text length 3\n"+legacyText+"}\n";
 assert(notecardLayout(embedded,legacyText,embedded.size()-2,prefix,suffix,1));assert(prefix==embeddedPrefix);
 assert(notecardReadText(legacyText,1,1,text) && text=="a\xf4\x80\x80\x80" "b" && utf8(text));
 assert(!notecardReadText(legacyText,1,0,text));
 assert(!notecardLayout(embedded+"trailing",legacyText,embedded.size()-2,prefix,suffix,1));
 assert(!notecardLayout(plain,"Legacy note!",plain.size()-3,prefix,suffix,1));
 assert(!notecardReadText(std::string("a\0b",3),1,1,text));
 assert(notecardReadText("modern 😀",2,0,text) && text=="modern 😀");
 std::cout<<"legacy v1 notecard read checks passed\n";
}
