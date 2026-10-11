#include "gpu_ws_hud_widget.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

struct Rect {
    std::int32_t x, y, w, h;
};

struct Aspect {
    std::int32_t numerator, denominator;
};

/* 16:10, 16:9, 21:9 and 32:9 as gpu_ws_configure reduces them. */
constexpr std::array<Aspect, 4> kAspects{{{5, 6}, {3, 4}, {4, 7}, {3, 8}}};

constexpr PsxWsHudWidget kList{16, 10, 128, 196, 0, 0};

/* The weapon list of the first mission with one weapon: band, lower bar and side bar as polygons, then right cap,
 * corner, top cap, arrow, two digits and icon as rectangles, and the name, which is text. */
constexpr std::array<Rect, 11> kPanel{{
    {20, 12, 106, 39}, {28, 37, 92, 14}, {18, 18, 10, 19}, {120, 37, 6, 14}, {18, 37, 10, 14}, {18, 12, 10, 6},
    {20, 24, 8, 9},    {110, 24, 8, 11}, {101, 24, 8, 11}, {34, 22, 38, 14}, {32, 39, 63, 11},
}};
constexpr std::size_t kPolygons = 3;
constexpr Rect kName = kPanel[10];
/* Beside the list: the health box's left cap and body, the ammunition box and a digit in it. */
constexpr std::array<Rect, 4> kOthers{{{120, 24, 14, 20}, {134, 23, 50, 22}, {12, 197, 56, 24}, {31, 200, 10, 15}}};

constexpr PsxWsHudWidget kPsionics{232, 10, 298, 170, 320, 0};
/* The psionics list with one power: backing, lower bar and side bar as polygons, then left cap, corner, top cap, arrow and icon. */
constexpr std::array<Rect, 8> kPsionicsPanel{{
    {235, 12, 57, 49}, {240, 47, 46, 14}, {286, 18, 10, 29}, {234, 47, 6, 14},
    {286, 47, 10, 14}, {286, 12, 10, 6},  {288, 29, 8, 9},   {240, 22, 24, 24},
}};
/* Beside it: the health box's right cap, and the psionic charge under it: head, its sparks, base and two digits. */
constexpr std::array<Rect, 6> kPsionicsOthers{{
    {184, 24, 14, 20}, {265, 170, 24, 30}, {259, 170, 34, 21}, {261, 200, 36, 10}, {283, 201, 10, 15}, {272, 201, 10, 15},
}};

/* The game's two ordering tables, the HUD node below each, a node of the world and the stack node text hangs on. */
constexpr std::uint32_t kTable = 0x800744D4u, kOtherTable = 0x80074ECCu;
constexpr std::uint32_t kHudNode = 0x000744D0u, kOtherHudNode = 0x00074EC8u;
constexpr std::uint32_t kWorldNode = 0x00074CCCu, kOtherWorldNode = 0x000756C4u, kStackNode = 0x001FFF08u;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    if (g_failures <= 20) std::cerr << "FAIL: " << message << '\n';
}

const PsxWsHudWidget *find(const Rect &rect) {
    return psx_ws_hud_widget_find(&kList, 1, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
}

/* A rectangle as the renderer maps it. */
Rect mapped(const Rect &rect, const Aspect &aspect, int percent) {
    Rect now = rect;
    psx_ws_hud_widget_rect(&kList, &now.x, &now.y, &now.w, &now.h, aspect.numerator, aspect.denominator, percent);
    return now;
}

PsxWsHudWidgets game(int layers = 1) {
    PsxWsHudWidgets hud{};
    hud.widgets[0] = kList;
    hud.count = 1;
    hud.layers = layers;
    return hud;
}

bool takes(PsxWsHudWidgets &hud, const Rect &rect, bool rectangle) {
    return psx_ws_hud_widgets_take(&hud, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, rectangle ? 1 : 0) != nullptr;
}

/* The game's main list up to and including its HUD layer, with or without the weapon list in it. */
void main_list(PsxWsHudWidgets &hud, std::uint32_t table, std::uint32_t world, std::uint32_t node, bool open) {
    psx_ws_hud_widgets_list(&hud, table);
    psx_ws_hud_widgets_node(&hud, world);
    psx_ws_hud_widgets_node(&hud, node);
    for (std::size_t piece = 0; open && piece + 1 < kPanel.size(); ++piece) takes(hud, kPanel[piece], piece >= kPolygons);
    psx_ws_hud_widgets_list_end(&hud);
}

void text_list(PsxWsHudWidgets &hud, std::uint32_t table) {
    psx_ws_hud_widgets_list(&hud, table);
    psx_ws_hud_widgets_node(&hud, kStackNode);
}

void test_the_box_decides_what_belongs() {
    for (const Rect &piece : kPanel) expect(find(piece) == &kList, "every primitive of the weapon list is inside its box");
    for (const Rect &other : kOthers) expect(find(other) == nullptr, "the health box and the ammunition box are not");
    expect(find({16, 10, 112, 186}) == &kList, "a primitive that fills the box belongs to it");
    expect(find({15, 10, 10, 10}) == nullptr && find({16, 9, 10, 10}) == nullptr && find({119, 20, 10, 10}) == nullptr &&
               find({20, 187, 10, 10}) == nullptr,
           "one that crosses any side of the box does not");
    expect(find({20, 20, 0, 10}) == nullptr && find({20, 20, 10, 0}) == nullptr && find({20, 20, -4, 10}) == nullptr,
           "an empty primitive belongs to nothing");
    expect(psx_ws_hud_widget_find(&kList, 0, 20, 20, 30, 30) == nullptr && psx_ws_hud_widget_find(nullptr, 0, 20, 20, 30, 30) == nullptr,
           "without widgets nothing is found");
    const std::array<PsxWsHudWidget, 2> two{{{200, 10, 300, 60, 320, 0}, kList}};
    expect(psx_ws_hud_widget_find(two.data(), 2, 20, 20, 30, 30) == &two[1] &&
               psx_ws_hud_widget_find(two.data(), 2, 210, 20, 230, 30) == &two[0],
           "each of several widgets holds its own primitives");
    const std::array<PsxWsHudWidget, 2> both{{kList, kPsionics}};
    const auto either = [&both](const Rect &rect) {
        return psx_ws_hud_widget_find(both.data(), 2, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
    };
    for (const Rect &piece : kPsionicsPanel) expect(either(piece) == &both[1], "every primitive of the psionics list is inside its box");
    for (const Rect &other : kPsionicsOthers) expect(either(other) == nullptr, "the health box and the psionic charge are in neither box");
    for (const Rect &piece : kPanel) expect(either(piece) == &both[0], "and the weapon list's primitives stay in theirs");
}

void test_the_pieces_keep_touching() {
    for (const Aspect &aspect : kAspects)
        for (int percent = 50; percent <= 100; ++percent) {
            const Rect band = mapped(kPanel[0], aspect, percent), bar = mapped(kPanel[1], aspect, percent);
            const Rect side = mapped(kPanel[2], aspect, percent), cap = mapped(kPanel[3], aspect, percent);
            const Rect corner = mapped(kPanel[4], aspect, percent), top = mapped(kPanel[5], aspect, percent);
            expect(bar.x + bar.w == cap.x && bar.y == cap.y && bar.h == cap.h, "the lower bar ends where its right cap begins");
            expect(corner.x + corner.w == bar.x && corner.y == bar.y && corner.h == bar.h, "and begins where the corner ends");
            expect(side.x == corner.x && side.w == corner.w && side.y + side.h == corner.y, "the side bar stands on the corner");
            expect(top.x == side.x && top.w == side.w && top.y + top.h == side.y, "and the top cap on the side bar");
            expect(band.x + band.w == cap.x + cap.w && band.y == top.y && band.y + band.h == bar.y + bar.h,
                   "the band ends with the right cap and spans the frame's height");
            const Rect tens = mapped(kPanel[8], aspect, percent), ones = mapped(kPanel[7], aspect, percent);
            expect(tens.x + tens.w <= ones.x && ones.x - (tens.x + tens.w) <= 1 && tens.y == ones.y && tens.h == ones.h,
                   "the two digits of the ammunition stay side by side, at most the pixel apart they were");
            for (const Rect &piece : kPanel) {
                const Rect now = mapped(piece, aspect, percent);
                expect(now.w >= 1 && now.h >= 1 && now.x >= 0 && now.y >= 0 && now.x <= piece.x && now.y <= piece.y &&
                           now.w <= piece.w && now.h <= piece.h,
                       "every piece moves toward the top left corner and never grows");
            }
        }
    /* Not a promise: 16:9 at 50% takes the edges 16, 17 and 18 to 6, 6 and 7, and the rows 11, 12 and 13 to 6, 6 and 7. */
    const Rect squeezed = mapped({16, 11, 1, 1}, kAspects[1], 50), beside = mapped({17, 11, 1, 1}, kAspects[1], 50);
    const Rect under = mapped({16, 12, 1, 1}, kAspects[1], 50);
    expect(squeezed.x == 6 && squeezed.w == 1 && beside.x == 6 && beside.w == 1,
           "a rectangle squeezed to no width keeps one pixel and then lies on its neighbour, it never leaves a hole");
    expect(squeezed.y == 6 && squeezed.h == 1 && under.y == 6 && under.h == 1, "and so does one squeezed to no height");
}

void test_exact_values() {
    const Rect ones = mapped(kPanel[7], kAspects[1], 100);
    expect(ones.x == 83 && ones.y == 24 && ones.w == 6 && ones.h == 11,
           "16:9 at the authored size: the last digit is squashed toward the left edge, not the centre");
    const Rect cap = mapped(kPanel[3], kAspects[1], 100);
    expect(cap.x == 90 && cap.w == 5 && cap.y == 37 && cap.h == 14, "and so is the right cap, three quarters as wide");
    const Rect band = mapped(kPanel[0], kAspects[1], 50);
    expect(band.x == 8 && band.y == 6 && band.w == 39 && band.h == 20, "16:9 at 50%: the band is 3/8 as wide and half as tall");
    std::int32_t x = 200, y = 100;
    const PsxWsHudWidget right{180, 0, 320, 240, 320, 240};
    psx_ws_hud_widget_corner(&right, &x, &y, 3, 4, 100);
    expect(x == 230 && y == 100, "a pivot on the right edge pulls a corner to the right, and the authored size keeps its row");
    psx_ws_hud_widget_corner(&right, &x, &y, 3, 4, 50);
    expect(x == 286 && y == 170, "a pivot at the bottom pulls it down at a smaller size");
}

void test_the_hud_hangs_below_the_table() {
    expect(psx_ws_hud_node(kHudNode, kTable, 2) && psx_ws_hud_node(0x000744CCu, kTable, 2),
           "the two nodes just below the table open the HUD's layers");
    expect(!psx_ws_hud_node(0x000744C8u, kTable, 2) && !psx_ws_hud_node(0x000744D4u, kTable, 2) &&
               !psx_ws_hud_node(0x000744D8u, kTable, 2) && !psx_ws_hud_node(0x00074CD0u, kTable, 2),
           "a node farther below, the table's own first entry and the entries above it do not");
    expect(psx_ws_hud_node(kHudNode, kTable, 1) && !psx_ws_hud_node(0x000744CCu, kTable, 1), "a game with one HUD node has one");
    expect(!psx_ws_hud_node(kHudNode, kTable, 0) && !psx_ws_hud_node(kHudNode, kTable, -1), "a game that declares none has none");
    expect(psx_ws_hud_node(0x800744D0u, kTable, 2) && psx_ws_hud_node(0xA00744D0u, 0x000744D4u, 2) &&
               psx_ws_hud_node(0x002744D0u, kTable, 2),
           "the segment and the mirror of an address do not matter");
    expect(!psx_ws_hud_node(0x1F0744D0u, kTable, 2) && !psx_ws_hud_node(kHudNode, 0x1F0744D4u, 2),
           "an address outside main RAM is no node of the table");
    expect(!psx_ws_hud_node(kHudNode, 0u, 2) && !psx_ws_hud_node(0u, 0u, 2) && !psx_ws_hud_node(0x001FFFFCu, 0u, 2),
           "without a table there is no HUD layer");
}

void test_a_frame_with_the_list_open() {
    PsxWsHudWidgets hud = game();
    psx_ws_hud_widgets_frame(&hud);
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_node(&hud, kWorldNode);
    expect(!takes(hud, kPanel[0], false) && !takes(hud, kPanel[7], true), "a layer of the world holds no widget, whatever lies in the box");
    psx_ws_hud_widgets_node(&hud, kHudNode);
    for (const Rect &other : kOthers) expect(!takes(hud, other, true), "in the HUD layer the health box and the ammunition box are no widget");
    for (std::size_t piece = 0; piece + 1 < kPanel.size(); ++piece)
        expect(takes(hud, kPanel[piece], piece >= kPolygons), "in the HUD layer every piece of the list is taken, polygons included");
    for (const Rect &other : kOthers) expect(!takes(hud, other, true), "and its neighbours still are not");
    psx_ws_hud_widgets_list_end(&hud);
    expect(!takes(hud, kPanel[0], false), "once the list has ended a polygon joins nothing");
    /* The table's word has moved on to the next frame's table by the time the name is printed. */
    text_list(hud, kOtherTable);
    expect(takes(hud, kName, true), "the name, printed afterwards as a list of its own, joins the list the frame showed");
    expect(!takes(hud, kName, false), "a polygon there does not");
    for (const Rect &other : kOthers) expect(!takes(hud, other, true), "nor a rectangle outside the box");
    psx_ws_hud_widgets_list_end(&hud);
}

void test_the_next_frame_starts_from_nothing() {
    PsxWsHudWidgets hud = game();
    psx_ws_hud_widgets_frame(&hud);
    main_list(hud, kTable, kWorldNode, kHudNode, true);
    text_list(hud, kOtherTable);
    expect(takes(hud, kName, true), "the frame showed the list");
    psx_ws_hud_widgets_list_end(&hud);

    psx_ws_hud_widgets_frame(&hud);
    psx_ws_hud_widgets_list(&hud, kOtherTable);
    psx_ws_hud_widgets_node(&hud, kOtherWorldNode);
    expect(!takes(hud, kName, true), "a rectangle of the next frame, before its HUD, joins nothing the last frame showed");
    psx_ws_hud_widgets_node(&hud, kOtherHudNode);
    psx_ws_hud_widgets_list_end(&hud);
    text_list(hud, kTable);
    expect(!takes(hud, kName, true), "text in the box of a list this frame did not show is not the list's");
    psx_ws_hud_widgets_list_end(&hud);

    /* Not a promise: a game that sets no drawing area between frames has nothing that forgets, so it cannot have widgets. */
    hud = game();
    main_list(hud, kTable, kWorldNode, kHudNode, true);
    main_list(hud, kOtherTable, kOtherWorldNode, kOtherHudNode, false);
    text_list(hud, kTable);
    expect(takes(hud, kName, true), "without a new drawing area a later frame still holds what an earlier one showed");
}

void test_what_does_not_show_a_widget() {
    PsxWsHudWidgets hud = game();
    psx_ws_hud_widgets_frame(&hud);
    text_list(hud, kTable);
    expect(!takes(hud, kName, true) && !takes(hud, kPanel[7], true), "a rectangle outside the HUD layer shows no widget by itself");
    psx_ws_hud_widgets_list_end(&hud);
    text_list(hud, kTable);
    expect(!takes(hud, kName, true), "and so nothing joins afterwards");

    hud = game();
    psx_ws_hud_widgets_frame(&hud);
    psx_ws_hud_widgets_list(&hud, 0u);
    psx_ws_hud_widgets_node(&hud, kHudNode);
    expect(!takes(hud, kPanel[0], false) && !takes(hud, kPanel[7], true), "a list drawn outside widescreen has no HUD layer");
    main_list(hud, kTable, kWorldNode, kHudNode, true);
    psx_ws_hud_widgets_list(&hud, 0u);
    psx_ws_hud_widgets_node(&hud, kStackNode);
    expect(!takes(hud, kName, true), "nor does it take what was shown before it");
    psx_ws_hud_widgets_list_end(&hud);
    text_list(hud, kOtherTable);
    expect(takes(hud, kName, true), "but it forgets nothing: the next list still holds what the frame showed");
    psx_ws_hud_widgets_frame(&hud);
    expect(!takes(hud, kName, true), "until the next drawing area");

    PsxWsHudWidgets none{};
    none.layers = 1;
    psx_ws_hud_widgets_list(&none, kTable);
    psx_ws_hud_widgets_node(&none, kHudNode);
    expect(!takes(none, kPanel[0], false) && !takes(none, kPanel[7], true), "a game without widgets has none to take");

    /* The table word has named one table so far. */
    hud = game();
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_node(&hud, kOtherHudNode);
    expect(!takes(hud, kPanel[0], false), "a node below a table the word has not named yet opens no HUD layer");
    psx_ws_hud_widgets_node(&hud, kHudNode);
    expect(takes(hud, kPanel[0], false), "and that one is");
    psx_ws_hud_widgets_node(&hud, kWorldNode);
    expect(!takes(hud, kPanel[0], false), "the layer after it is the world's again");
}

/* A save state from play: the game moves its table word on before the list it built is drawn, so a list from one
 * table begins while the word names the other. */
void test_the_table_word_may_run_ahead() {
    PsxWsHudWidgets hud = game();
    psx_ws_hud_widgets_frame(&hud);
    main_list(hud, kTable, kWorldNode, kHudNode, false);
    text_list(hud, kOtherTable);
    psx_ws_hud_widgets_list_end(&hud);

    psx_ws_hud_widgets_frame(&hud);
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_node(&hud, kOtherWorldNode);
    psx_ws_hud_widgets_list_end(&hud);
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_node(&hud, kOtherWorldNode);
    expect(!takes(hud, kPanel[0], false) && !takes(hud, kPanel[7], true), "a layer of the world is still no HUD");
    psx_ws_hud_widgets_node(&hud, kOtherHudNode);
    for (std::size_t piece = 0; piece + 1 < kPanel.size(); ++piece)
        expect(takes(hud, kPanel[piece], piece >= kPolygons), "the node below the table the word named before opens the HUD all the same");
    for (const Rect &other : kOthers) expect(!takes(hud, other, true), "and the list's neighbours stay out");
    psx_ws_hud_widgets_list_end(&hud);
    text_list(hud, kTable);
    expect(takes(hud, kName, true), "the name joins as in any other frame");
    psx_ws_hud_widgets_list_end(&hud);

    psx_ws_hud_widgets_frame(&hud);
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_node(&hud, kHudNode);
    expect(takes(hud, kPanel[0], false), "the node below the table the word names now opens it too");
    psx_ws_hud_widgets_list_end(&hud);

    /* A third table pushes the older of the two out. */
    psx_ws_hud_widgets_frame(&hud);
    psx_ws_hud_widgets_list(&hud, 0x80075000u);
    psx_ws_hud_widgets_node(&hud, kOtherHudNode);
    expect(!takes(hud, kPanel[0], false), "only the last two tables the word named have a HUD node");
    psx_ws_hud_widgets_node(&hud, kHudNode);
    expect(takes(hud, kPanel[0], false), "the newer of them still has");
    psx_ws_hud_widgets_node(&hud, 0x00074FFCu);
    expect(takes(hud, kPanel[7], true), "and so has the newest");

    /* A list without widgets names no table. */
    PsxWsHudWidgets none{};
    none.layers = 1;
    psx_ws_hud_widgets_list(&none, kTable);
    expect(none.tables[0] == 0u && none.tables[1] == 0u, "a game without widgets remembers no table");
    hud = game();
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_list(&hud, 0u);
    expect(hud.tables[0] == kTable && hud.tables[1] == 0u, "and a list outside widescreen names none either");
}

void test_several_widgets_and_layers() {
    PsxWsHudWidgets hud = game();
    hud.widgets[1] = {200, 10, 300, 60, 320, 0};
    hud.count = 2;
    const Rect other{210, 20, 20, 10};
    psx_ws_hud_widgets_frame(&hud);
    main_list(hud, kTable, kWorldNode, kHudNode, true);
    text_list(hud, kOtherTable);
    expect(takes(hud, kName, true) && !takes(hud, other, true), "a rectangle joins only the widget the frame showed, not another one");
    psx_ws_hud_widgets_list_end(&hud);
    psx_ws_hud_widgets_frame(&hud);
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_node(&hud, kHudNode);
    expect(psx_ws_hud_widgets_take(&hud, 210, 20, 230, 30, 1) == &hud.widgets[1], "each widget is taken as itself");
    psx_ws_hud_widgets_list_end(&hud);
    text_list(hud, kOtherTable);
    expect(takes(hud, other, true) && !takes(hud, kName, true), "and shown as itself");

    /* Two HUD lists in one frame, each showing its own widget: the second must not forget what the first showed. */
    psx_ws_hud_widgets_frame(&hud);
    main_list(hud, kTable, kWorldNode, kHudNode, true);
    psx_ws_hud_widgets_list(&hud, kTable);
    psx_ws_hud_widgets_node(&hud, kWorldNode);
    psx_ws_hud_widgets_node(&hud, kHudNode);
    expect(takes(hud, other, true), "a second HUD list of the frame shows the other widget");
    psx_ws_hud_widgets_list_end(&hud);
    text_list(hud, kOtherTable);
    expect(takes(hud, kName, true) && takes(hud, other, true), "and both stay shown until the next drawing area");
    psx_ws_hud_widgets_list_end(&hud);
    psx_ws_hud_widgets_frame(&hud);
    text_list(hud, kTable);
    expect(!takes(hud, kName, true) && !takes(hud, other, true), "which forgets both");

    /* Two HUD nodes one after the other: the second must not forget what the first showed. */
    PsxWsHudWidgets two = game(2);
    psx_ws_hud_widgets_frame(&two);
    psx_ws_hud_widgets_list(&two, kTable);
    psx_ws_hud_widgets_node(&two, kHudNode);
    expect(takes(two, kPanel[0], false), "the first HUD layer shows the list");
    psx_ws_hud_widgets_node(&two, 0x000744CCu);
    psx_ws_hud_widgets_list_end(&two);
    text_list(two, kOtherTable);
    expect(takes(two, kName, true), "passing on to the second HUD layer forgets nothing the first showed");
    PsxWsHudWidgets second = game(2);
    psx_ws_hud_widgets_list(&second, kTable);
    psx_ws_hud_widgets_node(&second, 0x000744CCu);
    expect(takes(second, kPanel[7], true), "and the second HUD layer is one in its own right");
}

}  // namespace

int main() {
    test_the_box_decides_what_belongs();
    test_the_pieces_keep_touching();
    test_exact_values();
    test_the_hud_hangs_below_the_table();
    test_a_frame_with_the_list_open();
    test_the_next_frame_starts_from_nothing();
    test_what_does_not_show_a_widget();
    test_the_table_word_may_run_ahead();
    test_several_widgets_and_layers();
    if (g_failures) return 1;
    std::cout << "GPU widescreen HUD widget tests passed\n";
    return 0;
}
