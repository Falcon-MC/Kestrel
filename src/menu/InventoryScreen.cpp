#include "menu/InventoryScreen.h"
#include "ui/Localization.h"
#include "util/Text.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace kestrel::menu {
namespace {
using namespace inventory;
constexpr ui::Color White { 255, 255, 255, 255 }, Ink { 76, 76, 76, 255 };
// What a closed inventory reads while it slides out, so it can't act on anything.
const InputState NoInput {};

double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::string nameOf(const HudItem& item) { return item.customName.empty() ? world::itemDisplayName(item.identifier) : item.customName; }
void appendUtf8(std::string& text, char32_t cp)
{
    if (cp < 32 || cp == 127 || cp > 0x10ffff) return;
    if (cp < 0x80) text += char(cp);
    else if (cp < 0x800) { text += char(0xc0 | (cp >> 6)); text += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { text += char(0xe0 | (cp >> 12)); text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63)); }
    else { text += char(0xf0 | (cp >> 18)); text += char(0x80 | ((cp >> 12) & 63)); text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63)); }
}

struct Layout {
    ui::Context& ui;
    float x, y, unit;
    ui::Rect rect(float a, float b, float w, float h) const { return { x + a * unit, y + b * unit, w * unit, h * unit }; }
    void image(float a, float b, float w, float h, const std::string& name, bool sliced = false, ui::Color color = White) const {
        // Context's default nineslice border uses menu units; preserve the classic screen's own pixel scale.
        const auto& sprite = ui.skin().sprite(name);
        if (sliced && sprite.valid) {
            ui::BorderImage border;
            border.valid = true; border.fill = true; border.sprite = name; border.slice = sprite.texels;
            border.width = { sprite.slice.left * unit, sprite.slice.top * unit, sprite.slice.right * unit, sprite.slice.bottom * unit };
            ui.borderImage(rect(a,b,w,h), border, color);
        } else ui.sprite(rect(a,b,w,h), name, color);
    }
    void text(float a, float b, std::string_view value, ui::Color color = Ink, bool shadow = false) const {
        if (shadow) ui.pixelTextScaled(value, x + (a + 1) * unit, y + (b + 1) * unit, unit, color, true);
        ui.pixelTextScaled(value, x + a * unit, y + b * unit, unit, color);
    }
};
}

void InventoryScreen::send(InventoryAction action, int slot, int value, bool all) { commands.push_back({ action, slot, value, all, {} }); }
void InventoryScreen::open() { active = true; dragging = false; searchFocused = false; hoveredSlot = -1; }
void InventoryScreen::close() { if (active) send(InventoryAction::Close); active = false; dragging = false; searchFocused = false; }
void InventoryScreen::reset() { active = false; dragging = false; searchFocused = false; commands.clear(); state = {}; }
std::vector<InventoryCommand> InventoryScreen::takeCommands() { std::vector<InventoryCommand> result; result.swap(commands); return result; }

bool InventoryScreen::handleKeys(const InputState& input, Key inventoryKey)
{
    if (!active) return false;
    if (input.escape || (!searchFocused && input.pressedKey == inventoryKey)) { close(); return true; }
    if (searchFocused) {
        if (input.isHeld(Key::Control) && input.pressedKey == Key::A) search.clear();
        if (input.backspace && !search.empty()) {
            do { search.pop_back(); } while (!search.empty() && (static_cast<unsigned char>(search.back()) & 0xc0) == 0x80);
            scrollRow = 0;
        }
        for (char32_t cp : input.text) if (search.size() < 128) { appendUtf8(search, cp); scrollRow = 0; }
        if (input.enter) searchFocused = false;
        return true;
    }
    return true;
}

void InventoryScreen::draw(ui::Context& ui, float width, float height, const std::function<void(float,float,float)>& player)
{
    const auto& input = active ? ui.input() : NoInput;
    const bool workbench = state.type == ContainerType::Workbench;
    const bool container = state.containerSize > 0;
    const bool furnace = state.type == ContainerType::Furnace || state.type == ContainerType::BlastFurnace || state.type == ContainerType::Smoker;
    const bool showBook = book && !container;
    const bool wide = showBook && creativeMode && wideCreative && !workbench;
    const float panelHeight = container && state.containerSize > 27 ? 220.0f : 166.0f;
    const float totalWidth = showBook ? 326.0f : 176.0f;
    float fit = std::min({ 1.0f, width / (totalWidth + 8), height / (panelHeight + 40) });
    float unit = std::max(1.0f, std::floor(fit * ui.pixelScale())) / ui.pixelScale();
    Layout l { ui, std::floor((width - totalWidth * unit) * 0.5f * ui.pixelScale()) / ui.pixelScale(),
        std::floor((height - panelHeight * unit) * 0.5f * ui.pixelScale()) / ui.pixelScale(), unit };
    const float right = showBook ? 150.0f : 0.0f;
    const float bookWidth = wide ? 326.0f : 146.0f;
    hoveredSlot = -1;
    const HudItem* tooltipItem = nullptr;
    std::string tooltip;
    bool overControl = false;
    auto button = [&](const std::string& id, float x, float y, float w, float h, const std::string& texture, const std::string& hoverTexture = std::string(), bool sliced = false) {
        auto hit = ui.interact(id, l.rect(x,y,w,h));
        l.image(x,y,w,h, hit.hovered && !hoverTexture.empty() ? hoverTexture : texture, sliced);
        overControl |= hit.hovered;
        return hit.clicked;
    };
    auto item = [&](const HudItem& value, float x, float y, bool count = true, uint8_t opacity = 255) {
        if (value.empty() || !itemIcon) return;
        HudSlot icon = itemIcon(value);
        if (!icon.icon.empty()) l.image(x+1,y+1,16,16,icon.icon,false,{255,255,255,opacity});
        if (icon.durability >= 0) {
            ui.fill(l.rect(x+3,y+14,13,2),{0,0,0,opacity});
            float fraction = std::clamp(icon.durability,0.0f,1.0f);
            ui.fill(l.rect(x+3,y+14,std::round(fraction*13),1),{uint8_t((1-fraction)*255),uint8_t(fraction*255),0,opacity});
        }
        if (count && value.count > 1) {
            std::string text = std::to_string(value.count);
            l.text(x+18-ui.measure(text,ui::TextStyle::Pixel),y+10,text,{255,255,255,opacity},true);
        }
    };
    auto slot = [&](int index, float x, float y, const std::string& overlay = std::string()) {
        const auto& value = state.slots[index];
        l.image(x,y,18,18,"ui/cell_image",true);
        const HudItem* ghost = nullptr;
        if (value.empty() && index >= inventory::Craft && index < inventory::Craft + 9) ghost = &state.recipeGhost[index - inventory::Craft];
        else if (value.empty() && index == inventory::Output) ghost = &state.recipeGhostOutput;
        if (ghost && !ghost->empty()) {
            ui.fill(l.rect(x+1,y+1,16,16),{170,40,40,200});
            item(*ghost,x,y,index == inventory::Output,160);
        }
        if (value.empty() && !overlay.empty()) l.image(x+1,y+1,16,16,overlay);
        item(value,x,y);
        if (ui.hovered(l.rect(x,y,18,18))) {
            hoveredSlot = index;
            ui.fill(l.rect(x+1,y+1,16,16),{255,255,255,90});
            if (!value.empty() && state.slots[inventory::Cursor].empty()) tooltipItem = &value;
        }
        if (dragging && std::find(dragSlots.begin(),dragSlots.end(),index) != dragSlots.end()) {
            HudItem preview = state.slots[inventory::Cursor];
            preview.count = std::max(1, preview.count / std::max(1,int(dragSlots.size())));
            preview.count += value.count;
            item(preview,x,y,true,190);
        }
    };

    if (showBook) {
        for (int i = 0; i < 5; ++i) {
            float x = i == 4 ? bookWidth-25 : float(i*25);
            bool selected = tab == i;
            std::string texture = "ui/TabTop" + std::string(selected ? "Front" : "Back") + (i==0?"LeftMost":i==4?"RightMost":"");
            if (button("inventory:tab:"+std::to_string(i),x,-25,25,29,texture,texture+"Hover",true)) { tab=i;scrollRow=0;searchFocused=i==4; }
            static const char* icons[] = {"icon_recipe_construction","icon_recipe_equipment","icon_recipe_item","icon_recipe_nature","magnifyingGlass"};
            l.image(x+4,-21,18,18,"ui/"+std::string(icons[i]));
        }
        l.image(0,0,bookWidth,166,"ui/dialog_background_opaque",true);
        if (!wide) l.image(146,0,4,166,"ui/center_fold",true);
        static const char* labels[] = {"craftingScreen.tab.construction","craftingScreen.tab.equipment","craftingScreen.tab.items","craftingScreen.tab.nature","craftingScreen.tab.search"};
        static const char* defaults[] = {"Construction","Equipment","Items","Nature","All"};
        std::string label = ui::tr(labels[tab], defaults[tab]);
        float labelRight = bookWidth - 9 - (creativeMode ? 0 : 30);
        l.text(std::max(9.0f,labelRight-ui.measure(label,ui::TextStyle::Pixel)),8,label);
        if (!creativeMode && button("inventory:filter",bookWidth-36,5,29,17,craftableOnly?"ui/craft_toggle_on":"ui/craft_toggle_off",craftableOnly?"ui/craft_toggle_on_hover":"ui/craft_toggle_off_hover")) { craftableOnly=!craftableOnly;scrollRow=0; }
        float gridY = 20;
        if (tab == 4) {
            ui::Rect searchRect=l.rect(7,20,bookWidth-14,16);
            ui.fill(searchRect,{0,0,0,255});
            ui.outline(searchRect,searchFocused?White:ui::Color{96,96,96,255},unit);
            if (input.mousePressed) searchFocused=ui.hovered(searchRect);
            overControl |= ui.hovered(searchRect);
            ui.setClip(searchRect.inset(2*unit));
            l.text(10,24,search+(searchFocused && int(nowSeconds()*2)%2==0?"_":""),White);
            ui.clearClip(); gridY=38;
        }
        const auto& catalog = creativeMode ? state.creative : state.recipes;
        struct Entry { const InventoryCatalogItem* value; bool group; };
        std::vector<Entry> filtered;
        std::set<int> groups;
        std::set<std::string> recipeOutputs;
        const int categories[] = {1,3,4,2,0};
        if (catalog) for (const auto& value : *catalog) {
            if (tab!=4 && value.category!=categories[tab] && !(value.category==0 && tab==0)) continue;
            if (!search.empty() && tab==4 && util::lowercase(nameOf(value.item)).find(util::lowercase(search))==std::string::npos
                && util::lowercase(value.item.identifier).find(util::lowercase(search))==std::string::npos) continue;
            bool craftable = std::find(state.craftable.begin(),state.craftable.end(),value.networkId)!=state.craftable.end();
            if (!creativeMode && craftableOnly && !craftable) continue;
            if (!creativeMode && !recipeOutputs.insert(value.item.identifier+"#"+std::to_string(value.item.aux)).second) continue;
            if (creativeMode && tab!=4 && !value.groupName.empty()) {
                if (groups.insert(value.group).second) filtered.push_back({&value,true});
                if (!expandedGroups.count(value.group)) continue;
            }
            filtered.push_back({&value,false});
        }
        const int columns = wide ? 17 : 7;
        const int rows = int((159-gridY)/18);
        int maxRow = std::max(0,(int(filtered.size())+columns-1)/columns-rows);
        if (ui.hovered(l.rect(0,0,bookWidth,166)) && input.wheel!=0) scrollRow -= input.wheel>0?1:-1;
        scrollRow=std::clamp(scrollRow,0,maxRow);
        ui.fill(l.rect(6,gridY-1,bookWidth-12,160-gridY),{94,94,94,255});
        ui.setClip(l.rect(7,gridY,columns*18,rows*18));
        for (int i=0;i<rows*columns;++i) {
            int at=scrollRow*columns+i;
            if (at>=int(filtered.size())) break;
            const auto& entry=filtered[at]; const auto& value=*entry.value;
            float x=7+float(i%columns)*18,y=gridY+float(i/columns)*18;
            bool craftable=creativeMode || std::find(state.craftable.begin(),state.craftable.end(),value.networkId)!=state.craftable.end();
            const char* background=entry.group?(expandedGroups.count(value.group)?"ui/recipe_book_dark_button_pressed":"ui/recipe_book_light_button")
                :creativeMode && tab!=4 && !value.groupName.empty()?"ui/recipe_book_dark_button":craftable?"ui/recipe_book_item_bg":"ui/recipe_book_red_button";
            l.image(x,y,18,18,background,true);
            item(value.item,x,y,false);
            if (entry.group) l.image(x+10,y+10,7,7,expandedGroups.count(value.group)?"ui/recipe_book_collapse_icon":"ui/recipe_book_expand_icon");
            if (ui.hovered(l.rect(x,y,18,18))) {
                overControl=true;
                ui.fill(l.rect(x+1,y+1,16,16),{255,255,255,90});
                if (entry.group) tooltip=ui::tr(value.groupName,value.groupName); else tooltipItem=&value.item;
                if (input.mousePressed || input.rightMousePressed) {
                    if (entry.group) { if (!expandedGroups.erase(value.group)) expandedGroups.insert(value.group); }
                    else if (creativeMode) send(InventoryAction::Creative,input.isHeld(Key::Shift)?0:-1,value.networkId,input.mousePressed);
                    else send(InventoryAction::SelectRecipe,-1,value.networkId);
                }
                if (creativeMode && input.pressedKey>=Key::Num1 && input.pressedKey<=Key::Num9)
                    send(InventoryAction::Creative,int(input.pressedKey)-int(Key::Num1),value.networkId,true);
            }
        }
        ui.clearClip();
        float railHeight=159-gridY;
        l.image(bookWidth-12,gridY,6,railHeight,"ui/ScrollRail",true);
        float thumb=std::max(10.0f,railHeight*rows/std::max(rows,maxRow+rows));
        float thumbY=gridY+(maxRow?scrollRow*(railHeight-thumb)/maxRow:0);
        l.image(bookWidth-12,thumbY,6,thumb,"ui/ScrollBox",true);
        if (input.mouseDown && ui.hovered(l.rect(bookWidth-13,gridY,8,railHeight))) {
            scrollRow=std::clamp(int(std::round(((ui.mouseY()-l.y)/unit-gridY-thumb/2)/std::max(1.0f,railHeight-thumb)*maxRow)),0,maxRow);
            overControl=true;
        }
        ui.fill(l.rect(6,gridY-1,bookWidth-18,1),{64,64,64,255});
    }

    if (!wide) {
        l.image(right,0,176,panelHeight,"ui/dialog_background_opaque",true);
        if (!container) {
            if (!workbench) {
                l.image(right+25,7,54,72,"ui/player_preview_border",true);
                ui.fill(l.rect(right+26,8,52,70),{0,0,0,255});
                ui.setClip(l.rect(right+26,8,52,70));
                player(l.x+(right+52)*unit,l.y+13*unit,1.875f*unit);
                ui.clearClip();
                const char* armor[] = {"helmet","chestplate","leggings","boots"};
                for(int i=0;i<4;++i) slot(Armor+i,right+7,7+18.0f*i,"ui/empty_armor_slot_"+std::string(armor[i]));
                slot(Offhand,right+79,61,"ui/empty_armor_slot_shield");
            }
            int size=workbench?3:2;
            float gx=right+(workbench?29:96),gy=workbench?16:18;
            std::string label=ui::tr("container.crafting","Crafting");
            l.text(gx+(size*18-ui.measure(label,ui::TextStyle::Pixel))*0.5f,workbench?5:7,label);
            for(int i=0;i<size*size;++i) slot(Craft+i,gx+(i%size)*18,gy+(i/size)*18);
            l.image(right+(workbench?90:134),workbench?35.5f:29.0f,workbench?22:16,workbench?15:13,workbench?"ui/arrow_large":"ui/arrow");
            slot(Output,right+(workbench?123:152),workbench?30:26);
            if(workbench) l.text(right+8,72,ui::tr("container.inventory","Inventory"));
        } else if (furnace) {
            std::string label=ui::tr(state.type==ContainerType::Smoker?"container.smoker":state.type==ContainerType::BlastFurnace?"container.blast_furnace":"container.furnace","Furnace");
            l.text(right+(176-ui.measure(label,ui::TextStyle::Pixel))*0.5f,7,label);
            slot(Container,right+55,16);slot(Container+1,right+55,52);slot(Container+2,right+115,34);
            l.image(right+79,35,22,16,"ui/arrow_inactive");
            ui.setClip(l.rect(right+79,35,22*state.furnaceProgress,16));l.image(right+79,35,22,16,"ui/arrow_active");ui.clearClip();
            l.image(right+57,36,14,14,"ui/flame_empty_image");
            if(state.furnaceFlame>0) {ui.setClip(l.rect(right+57,50-14*state.furnaceFlame,14,14*state.furnaceFlame));l.image(right+57,36,14,14,"ui/flame_full_image");ui.clearClip();}
            l.text(right+8,72,ui::tr("container.inventory","Inventory"));
        } else {
            const char* title=state.containerSize>27?"container.chestDouble":state.type==ContainerType::Hopper?"container.hopper":state.type==ContainerType::Dispenser?"container.dispenser":state.type==ContainerType::Dropper?"container.dropper":"container.chest";
            l.text(right+7,11,ui::tr(title,state.containerSize>27?"Large Chest":"Chest"));
            int columns=state.containerSize==9?3:state.containerSize==5?5:9;
            float start=right+(176-columns*18)*0.5f;
            for(int i=0;i<state.containerSize;++i)slot(Container+i,start+(i%columns)*18,21+(i/columns)*18);
        }
        for(int i=9;i<36;++i)slot(i,right+7+((i-9)%9)*18,panelHeight-80+((i-9)/9)*18);
        for(int i=0;i<9;++i)slot(i,right+7+i*18,panelHeight-23);
    } else {
        l.image(76,161,174,28,"ui/dialog_background_opaque_overlap_bottom",true);
        for(int i=0;i<9;++i)slot(i,82+i*18,165);
    }

    if (!container) {
        float toolbarWidth=creativeMode?122:95;
        float tx=totalWidth-toolbarWidth;
        l.image(tx,-27,toolbarWidth,30,"ui/toolbar_background",true);
        float bx=tx+8;
        if(creativeMode) {
            if(button("inventory:wide",bx,-20,25,18,wide?"ui/recipe_book_button_borderless_lightpressed":"ui/recipe_book_button_borderless_light","ui/recipe_book_button_borderless_lighthover",true)){wideCreative=true;book=true;}
            l.image(bx+4,-21,18,18,"ui/creative_icon");bx+=27;
        }
        if(button("inventory:book",bx,-20,25,18,showBook&&!wide?"ui/recipe_book_button_borderless_lightpressed":"ui/recipe_book_button_borderless_light","ui/recipe_book_button_borderless_lighthover",true)){book=true;wideCreative=false;}
        l.image(bx+4,-21,18,18,"ui/recipe_book_icon");bx+=26;
        if(button("inventory:survival",bx,-20,25,18,!showBook?"ui/recipe_book_button_borderless_lightpressed":"ui/recipe_book_button_borderless_light","ui/recipe_book_button_borderless_lighthover",true)){book=false;wideCreative=false;}
        l.image(bx+4,-21,18,18,"ui/inventory_icon");bx+=29;
        if(button("inventory:close",bx,-21,12,12,"ui/close_button_default_light","ui/close_button_hover_light"))close();
    } else if(button("inventory:close",right+160,5,12,12,"ui/close_button_default","ui/close_button_hover"))close();

    bool outside=!l.rect(0,-27,totalWidth,panelHeight+27+(wide?24:0)).contains(ui.mouseX(),ui.mouseY());
    if(!searchFocused && hoveredSlot>=0) {
        if(input.pressedKey>=Key::Num1 && input.pressedKey<=Key::Num9)send(InventoryAction::HotbarSwap,hoveredSlot,int(input.pressedKey)-int(Key::Num1));
        if(input.pressedKey==Key::Q)send(InventoryAction::Drop,hoveredSlot,0,input.isHeld(Key::Control));
    }
    if(input.mousePressed || input.rightMousePressed) {
        bool secondary=input.rightMousePressed;
        if(hoveredSlot>=0) {
            searchFocused=false;
            if(input.isHeld(Key::Shift))send(InventoryAction::QuickMove,hoveredSlot);
            else if(!secondary && lastClickedSlot==hoveredSlot && nowSeconds()-lastClick<0.3 && !state.slots[inventory::Cursor].empty())send(InventoryAction::Collect,hoveredSlot);
            else if(secondary || state.slots[inventory::Cursor].empty() || hoveredSlot==Output)send(secondary?InventoryAction::Secondary:InventoryAction::Primary,hoveredSlot);
            else {dragging=true;dragStart=hoveredSlot;dragSlots={hoveredSlot};}
            lastClickedSlot=hoveredSlot;lastClick=nowSeconds();
        } else if(outside && !overControl)send(InventoryAction::Drop,inventory::Cursor,0,!secondary);
    }
    if(dragging) {
        if(hoveredSlot>=0 && hoveredSlot!=Output && std::find(dragSlots.begin(),dragSlots.end(),hoveredSlot)==dragSlots.end())dragSlots.push_back(hoveredSlot);
        if(!input.mouseDown) {
            if(dragSlots.size()==1)send(InventoryAction::Primary,dragStart);
            else commands.push_back({InventoryAction::Distribute,-1,0,true,dragSlots});
            dragging=false;dragSlots.clear();
        }
    }
    if(!state.slots[inventory::Cursor].empty()) {
        float cx=(ui.mouseX()-l.x)/unit-8,cy=(ui.mouseY()-l.y)/unit-8;
        item(state.slots[inventory::Cursor],cx,cy);tooltipItem=nullptr;tooltip.clear();
    }
    if(tooltipItem) {
        tooltip=nameOf(*tooltipItem);
        for(const auto& line:tooltipItem->lore)tooltip+='\n'+line;
    }
    if(!tooltip.empty() && !dragging) {
        std::vector<std::string> lines;
        for(size_t start=0;start<=tooltip.size();) {size_t end=tooltip.find('\n',start);lines.push_back(tooltip.substr(start,end-start));if(end==std::string::npos)break;start=end+1;}
        float tw=0;for(const auto& line:lines)tw=std::max(tw,ui.measure(line,ui::TextStyle::Pixel));
        float th=lines.size()*10.0f;
        float x=ui.mouseX()+12*unit,y=ui.mouseY()-12*unit;
        if(x+(tw+8)*unit>width)x=ui.mouseX()-(tw+12)*unit;
        x=std::max(4*unit,x);y=std::clamp(y,4*unit,std::max(4*unit,height-(th+8)*unit));
        l.image((x-l.x)/unit-4,(y-l.y)/unit-4,tw+8,th+7,"ui/purpleBorder",true);
        for(size_t i=0;i<lines.size();++i) {
            ui::Color color=i==0?White:ui::Color{170,170,170,255};
            ui.pixelTextScaled(lines[i],x+unit,y+(i*10+1)*unit,unit,color,true);
            ui.pixelTextScaled(lines[i],x,y+i*10*unit,unit,color);
        }
    }
}
}
