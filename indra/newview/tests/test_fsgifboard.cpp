#include "fsgifboardmodel.h"
#include <cassert>
#include <climits>
using namespace fs_gif_board;
int main()
{
    assert(boundedXML("<?xml version=\"1.0\"?><llsd><map><key>items</key><array/></map></llsd>"));
    assert(!boundedXML("<!DOCTYPE x><llsd/>")&&!boundedXML("<llsd><map>")&&!boundedXML(std::string(MaxLibraryBytes+1,'x')));
    std::vector<Item> library;
    Item item;item.id="one";item.url="https://images.sodie.online/raw/cat.gif?token=AbC";item.title="Cat";item.tags="hello wave";item.collection="Reactions";
    assert(upsert(library,item));item.id="duplicate";item.favourite=true;assert(upsert(library,item)&&library.size()==1&&library[0].id=="one");
    auto second=item;second.id="two";second.url="https://images.sodie.online/raw/two.gif";assert(upsert(library,second));
    second.url=item.url;assert(!upsert(library,second)&&library.size()==2);library.pop_back();
    assert(filter(library,"WAVE","Reactions",true).size()==1&&filter(library,"cat","Other",false).empty());
    auto bad=item;bad.url="file:///secret.gif";assert(!valid(bad));bad=item;bad.url="https://static.klipy.com/a.gif";assert(!valid(bad));
    bad=item;bad.title=std::string(129,'x');assert(!valid(bad));
    Item favourite;favourite.id="klipy";favourite.provider="klipy";favourite.slug="happy-cat-1";assert(upsert(library,favourite));
    assert(filter(library,"","",false).size()==1);favourite.url="https://static.klipy.com/a.gif";assert(!valid(favourite)); // Never persist provider media URLs.
    assert(visiblePercent({0,0,100,100},{{0,0,100,79}})==79);
    assert(visiblePercent({0,0,100,100},{{0,0,100,80}})==80);
    assert(visiblePercent({0,0,100,100},{{20,0,100,100},{0,10,100,100}})==72);
    assert(visiblePercent({0,0,0,0},{})==0&&visiblePercent({INT_MIN,INT_MIN,INT_MAX,INT_MAX},{})==100);
    assert(!autoplay(0,79,80,false,false,false,false)&&autoplay(0,80,80,false,false,false,false));
    assert(!autoplay(0,100,80,true,true,true,false)&&!autoplay(0,100,80,true,true,false,true));
    assert(autoplay(1,1,80,true,false,false,false)&&autoplay(1,10,80,false,true,false,false));
    assert(!autoplay(2,100,80,true,true,false,false)&&!autoplay(1,0,80,true,true,false,false));
    Identity target{"a","login","grid","editor","private-im",1};auto other=target;assert(target==other);
    other.destination="Nearby:0";assert(!(target==other));other=target;other.login="new";assert(!(target==other));other=target;++other.generation;assert(!(target==other));
    auto full=std::vector<Item>(MaxBookmarks,item);auto next=item;next.id="two";next.url="https://images.sodie.online/raw/two.gif";assert(!upsert(full,next));
}
