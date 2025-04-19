#include <curses.h>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <optional>
#include <filesystem>
#include <unordered_set>

using vecstr = std::vector<std::string>;
namespace fsystem = std::filesystem;

//
// +-----------------------+
// | Core class definition |
// +-----------------------+
//

class Melt
{
  private:
    vecstr lines{};

    vecstr front{};
    vecstr back{};

    int fstate = 0;
    int edmode = 0;

    std::string fname{};
    std::string sbar{};
    std::string smessage{};

    std::string cmd{};
    std::string last_cmd{};

    unsigned cx = 0, cy = 0;
    unsigned ox = 0, oy = 0;

    unsigned mx = 0, my = 0;
    unsigned ax = 0, ay = 0;

    bool is_running = false;
    bool is_ws_changed = false;

    // methods

    std::optional<std::string> load(std::string_view fn);
    std::optional<std::string> save(std::string_view fn);

    void processEvents();
    void processCmd(std::string_view c);
    void mvCursor(int xd, int yd);
    
    void scrollToFit();
    void scrollUp(unsigned d = 1);
    void scrollDown(unsigned d = 1);
    void scrollLeft(unsigned d = 1);
    void scrollRight(unsigned d = 1);

    void insCh(unsigned x, unsigned y, char c);
    void rmCh(unsigned x, unsigned y);
    void insLn(unsigned y, std::string l);
    void rmLn(unsigned y);
    void jnLn(unsigned y);
    void spLn(unsigned x, unsigned y);

    void update();
    void render();

    // helpers
    static void cCheck(int r);
    static bool valFn(std::string_view fn);

    static std::string escPercent(std::string_view s);
    static std::string expandTabs(std::string_view s);
    static size_t visualLength(std::string_view s);

  public:
    virtual ~Melt() = default;

    void init(int argc, char **argv);
    void shutdown();

    void run()
    {
        is_running = true;

        while (is_running)
        {
            update();
            render();

            processEvents();
        }
    }
};

//
//
// +---------------+
// | main function |
// +---------------+
//
//

int main(int argc, char **argv)
{
    try
    {
        Melt med{};

        med.init(argc, argv);
        med.run();
        med.shutdown();
    }
    catch (const std::runtime_error &e)
    {
        std::printf("Fatal error: %s", (e.what() == nullptr ? "unknown error!" : e.what()));
        return -1;
    }
    catch (...)
    {
        std::printf("Failed due to unknown error!");
        return -1;
    }

    return 0;
}

//
//
// +-----------------------------+
// | Core methods implementation |
// +-----------------------------+
//
//

//
// initialization and cleanup handling
//

void Melt::init(int argc, char **argv)
{
    // file operations
    lines.push_back("");

    auto fback = [this](std::string msg = "") -> void {
        fname.clear();
        fstate = 0;
        smessage = std::move(msg);
    };

    if (argc > 1)
    {
        fname = argv[1];
        fstate = 1;
        if (fname.empty())
            fback();
        else if (!valFn(fname))
            fback("Invalid filename!");
        else if (fsystem::path p = fname; !fsystem::exists(fname))
            fstate = 0;
        else if (auto r = load(fname); r.has_value())
            fback(r.value());
    }
    else
        fback();

    // initialize curses
    if (initscr() == nullptr)
        throw std::runtime_error{"Curses initialization failed!"};

    // disable line-buffering and special key detection
    cCheck(raw());
    cCheck(keypad(stdscr, true));
    cCheck(noecho());

    curs_set(1);

    start_color();
    init_pair(1, COLOR_BLACK, COLOR_WHITE);

    getmaxyx(stdscr, my, mx);
    ax = mx;
    ay = my - 2;

    if (ax < 40 || ay < 12)
        throw std::runtime_error{"Terminal size too small!"};

    back.resize(ay, std::string(ax, ' '));
    front.resize(ay, std::string(ax, ' '));

    // force initial redraw
    is_ws_changed = true;
}

void Melt::shutdown() { endwin(); }

//
// file I/O
//

std::optional<std::string> Melt::load(std::string_view fn)
{
    if (fn.empty())
        return {"Empty filename!"};
    std::ifstream f;

    f.open(fn);
    if (!f.is_open())
        return {"Failed to open " + std::string(fn) + " for reading!"};
    lines.clear();

    std::string s{};
    while (std::getline(f, s))
    {
        lines.push_back(s);
    }
    if (f.bad() || (!f.eof() && f.fail()))
    {
        lines.clear();
        lines.push_back("");
        return {"Failed to read from " + std::string(fn) + " due to unknown error!"};
    }

    f.close();

    if (lines.empty())
        lines.push_back("");
    return std::nullopt;
}

std::optional<std::string> Melt::save(std::string_view fn)
{
    if (fn.empty())
        return {"Empty filename!"};
    std::ofstream f;

    f.open(fn);
    if (!f.is_open())
        return {"Failed to open " + std::string(fn) + " for writing!"};

    for (const auto &l : lines)
        f << l << "\n";

    f.close();

    return std::nullopt;
}

//
// text editing operations
//

void Melt::insCh(const unsigned x, const unsigned y, char c)
{
    if (y < lines.size() && x <= lines[y].length())
        lines[y].insert(x, 1, c);
}

void Melt::rmCh(const unsigned x, const unsigned y)
{
    if (y < lines.size() && x < lines[y].length())
        lines[y].erase(x, 1);
}

void Melt::insLn(const unsigned y, std::string l)
{
    if (y <= lines.size())
        lines.insert(lines.begin() + y, std::move(l));
}

void Melt::rmLn(const unsigned y)
{
    if (y < lines.size())
        lines.erase(lines.begin() + y);
}

void Melt::jnLn(const unsigned y)
{
    if (y < lines.size() - 1)
    {
        lines[y] += lines[y + 1];
        rmLn(y + 1);
    }
}

void Melt::spLn(const unsigned x, const unsigned y)
{
    if (y < lines.size() && x <= lines[y].length())
    {
        std::string s = lines[y].substr(x);
        lines[y].erase(x);
        insLn(y + 1, s);
    }
}

//
// cursor operations
//

void Melt::mvCursor(int xd, int yd)
{
    int nx = static_cast<int>(cx) + xd;
    int ny = static_cast<int>(cy) + yd;

    if (ny < 0)
        ny = 0;
    else if (ny >= static_cast<int>(lines.size()))
        ny = lines.size() - 1;

    if (ny != static_cast<int>(cy) && ny >= 0 && ny < static_cast<int>(lines.size()))
        nx = std::min(nx, static_cast<int>(lines[ny].length()));

    if (nx < 0)
    {
        if (ny > 0)
        {
            --ny;
            nx = lines[ny].length();
        }
        else
            nx = 0;
    }
    else if (nx > static_cast<int>(lines[ny].length()))
    {
        nx = 0;
        if (ny < static_cast<int>(lines.size()) - 1)
            ++ny;
    }

    cx = static_cast<unsigned>(nx);
    cy = static_cast<unsigned>(ny);
    scrollToFit();
}

void Melt::scrollToFit()
{
    unsigned d = 1;
    // horizontal
    if (cx < ox)
    {
        d = ox - cx;
        scrollLeft(d);
    }
    else if (cx >= ox + ax)
    {
        d = cx - (ox + ax) + 1;
        scrollRight(d);
    }

    // vertical
    if (cy < oy)
    {
        d = oy - cy;
        scrollUp(d);
    }
    else if (cy >= oy + ay)
    {
        d = cy - (oy + ay) + 1;
        scrollDown(d);
    }
}

void Melt::scrollUp(unsigned d)
{
    if (oy >= d)
        oy -= d;
}

void Melt::scrollDown(unsigned d)
{
    if (oy + d < lines.size())
        oy += d;
}

void Melt::scrollLeft(unsigned d)
{
    if (ox >= d)
        ox -= d;
    else
        ox = 0;
}

void Melt::scrollRight(unsigned d)
{
    if (front.empty())
        return;

    // find maximum line length among visible lines
    int max_length = 0;
    for (size_t i = 0; i < front.size() && i + oy < lines.size(); ++i)
    {
        size_t idx = i + oy;
        if (idx < lines.size())
            max_length = std::max(max_length, static_cast<int>(visualLength(lines[idx])));
    }

    if (ox + d < static_cast<unsigned>(max_length))
        ox += d;
}

//
// process keyboard events
//

void Melt::processEvents()
{
    int ch = getch();

    switch (ch)
    {
    case KEY_UP:
        // arrow up
        if (edmode == 0)
        {
            if (cy == 0)
                mvCursor(-cx, 0);
            else
                mvCursor(0, -1);
        }
        break;
    case KEY_DOWN:
        // arrow down
        if (edmode == 0)
        {
            if (cy == lines.size() - 1)
                mvCursor(lines[cy].length() - cx, 0);
            else
                mvCursor(0, 1);
        }
        break;
    case KEY_LEFT:
        // arrow left
        if (edmode == 0 && cx > 0)
            mvCursor(-1, 0);
        break;
    case KEY_RIGHT:
        // arrow right
        if (edmode == 0 && cx < lines[cy].length())
            mvCursor(1, 0);
        break;
    case KEY_BACKSPACE:
    case 127:
    case 8:
        // BACKSPACE key
        if (edmode == 0)
        {
            if (cx == 0 && cy > 0)
            {
                unsigned prevlen = lines[cy - 1].length();
                jnLn(cy - 1);
                mvCursor(0, -1);
                mvCursor(prevlen, 0);
            }
            else if (cx > 0)
            {
                rmCh(cx - 1, cy);
                mvCursor(-1, 0);
            }
            fstate = 2;
        }
        break;
    case KEY_ENTER:
    case 10:
    case 13:
        // ENTER key
        if (edmode == 0)
        {
            spLn(cx, cy);
            mvCursor(-lines[cy].length(), 1);
            fstate = 2;
        }
        else if (edmode == 1)
        {
            processCmd(cmd);
            cmd.clear();
            edmode = 0;
        }
        break;
    case 9:
        // TAB key
        if (edmode == 0)
        {
            for (int i = 0; i < 4; ++i) insCh(cx, cy, ' ');
            mvCursor(4, 0);
            fstate = 2;
        }
        break;
    case 27:
        // ESC key
        if (edmode == 0)
            edmode = 1;
        else if (edmode == 1)
            edmode = 0;
            cmd.clear();
        break;
    default:
        if (ch >= 32 && ch <= 126)
        {
            if (edmode == 0)
            {
                // basic characters
                insCh(cx, cy, ch);
                mvCursor(1, 0);
                fstate = 2;
            }
            else if (edmode == 1)
                cmd += ch;
        }
        break;
    }
}

//
// execute command
//

void Melt::processCmd(std::string_view c)
{
    if (c.empty())
        return;

    for (auto ch : c)
    {
        if (ch == '.')
            processCmd(last_cmd);
        else if (ch == 's')
        {
            if (auto ret = save(fname); ret.has_value())
                smessage = ret.value();
            else
            {
                smessage = "Successfully written to " + fname;
                fstate = 1;
            }
        }
        else if (ch == 'w')
        {
            smessage = "Write file: ";
            smessage.resize(mx, ' ');
            mvprintw(my - 1, 0, escPercent(smessage).c_str());
            move(my - 1, 12);

            std::string fn{};
            char chr = 0;
            while (chr != 27)
            {
                chr = getch();
                switch (chr) 
                {
                case KEY_ENTER:
                case 10:
                case 13:
                    if (fn.empty())
                        smessage = "Empty filename!";
                    else if (!valFn(fn))
                        smessage = "Invalid filename!";
                    else if (auto ret = save(fn); ret.has_value())
                        smessage = ret.value();
                    else
                    {
                        fname = fn;
                        smessage = "Successfully written to " + fname;
                        fstate = 1;
                    }
                    chr = 27;
                    break;
                case KEY_BACKSPACE:
                case 127:
                case 8:
                    fn = fn.substr(0, fn.length() - 1);
                    mvdelch(my - 1, 12 + fn.length());
                    break;
                default:
                    if (chr >= 32 && chr <= 126)
                    {
                        fn += chr;
                        addch(chr);
                    }
                    break;
                }
                move(my - 1, 12 + fn.length());
            }
        }
        else if (ch == 'q')
        {
            if (fstate == 2) smessage = "No write since last change (use Q to override)!";
            else is_running = false;
        }
        else if (ch == 'Q')
            is_running = false;
        else if (ch == 'd')
        {
            if (cy > 0)
            {
                rmLn(cy);
                mvCursor(0, -1);
                fstate = 2;
            }
            else
                smessage = "Only one line left!";
        }
        else
        {
            smessage = "Unknown command: " + std::string(1, ch);
            return;
        }
    }
    last_cmd = cmd;
}

//
// rendering - use double buffer technique
//

void Melt::update()
{
    // check for window size changes
    unsigned nx, ny;
    getmaxyx(stdscr, ny, nx);
    if (mx != nx || my != ny)
    {
        is_ws_changed = true;
        mx = nx;
        my = ny;
        ax = mx;
        ay = my - 2;

        if (ax < 40 || ay < 12)
            throw std::runtime_error{"Terminal size too small!"};

        back.resize(ay, std::string(ax, ' '));
        front.resize(ay, std::string(ax, ' '));
    }

    auto getVisible = [this](std::string s) -> std::string {
        std::string estr = expandTabs(std::move(s));
        std::string vis = std::string(ax, ' ');
        if (estr.length() > ox)
        {
            size_t cp_len = std::min(static_cast<size_t>(ax), estr.length() - ox);
            vis.assign(estr, ox, cp_len);
            if (cp_len < static_cast<size_t>(ax))
                vis.resize(ax, ' ');
        }
        return vis;
    };

    for (size_t i = 0; i < ay; ++i)
    {
        if (i + static_cast<size_t>(oy) < lines.size())
            back[i] = getVisible(lines[i + static_cast<size_t>(oy)]);
        else
            back[i] = "~" + std::string(ax - 1, ' ');
    }

    // update status bar and system message
    std::string dname = (fname.length() >= 23 ? fname.substr(0, 20) + "..." : fname);
    std::string info = (dname.empty() ? "[NEW FILE]" : dname) + (fstate == 2 ? "[+]" : "");
    std::string position = "Ln " + std::to_string(cy + 1) + ", Col " + std::to_string(cx + 1);
    sbar = info + std::string(mx - info.length() - position.length(), ' ') + position;

    smessage.resize(mx, ' ');
}

void Melt::render()
{
    curs_set(0);
    if (is_ws_changed)
    {
        resize_term(0, 0);
        clear();

        front = back;
        // complete redraw
        for (int i = 0; i < static_cast<int>(front.size()); ++i)
            mvprintw(i, 0, escPercent(front[i]).c_str());

        is_ws_changed = false;
    }
    else
    {
        if (front.size() != back.size())
            front.resize(back.size());

        for (int i = 0; i < static_cast<int>(back.size()); ++i)
        {
            if (i >= static_cast<int>(front.size()) || front[i] != back[i])
            {
                front[i] = back[i];

                mvprintw(i, 0, escPercent(front[i]).c_str());
            }
        }
    }

    attron(COLOR_PAIR(1));
    // render status bar and system message
    mvprintw(my - 2, 0, escPercent(sbar).c_str());
    attroff(COLOR_PAIR(1));
    mvprintw(my - 1, 0, escPercent(smessage).c_str());

    int visual_cx = 0;
    if (cy < lines.size()) {
        std::string cursor_str = lines[cy].substr(0, cx);
        visual_cx = static_cast<int>(visualLength(cursor_str));
    }
    move(cy - oy, visual_cx - ox);
    curs_set(1);

    refresh();
}

//
//
// +-----------------------------+
// | Core helpers implementation |
// +-----------------------------+
//
//

//
// check if curses library functions return error
//

void Melt::cCheck(int r)
{
    if (r == ERR)
        throw std::runtime_error{"Curses dependencies failed!"};
}

//
// check if the filename given is valid across various platforms
//

inline constexpr int MAX_FN_LEN = 255;
bool Melt::valFn(std::string_view fn)
{
    if (fn.empty())
        return false;

    // maximum path length check
    if (fn.length() > MAX_FN_LEN)
        return false;

    // reserved Windows filenames (case-insensitive)
    static const std::unordered_set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

    // disallowed characters on Windows/macOS/Linux
    static const std::string invalid_chars = R"(<>:"/\|?*)";

    // check for leading/trailing spaces or periods
    if (fn.front() == ' ' || fn.back() == ' ' || fn.back() == '.')
        return false;

    // check for invalid characters
    for (char c : fn)
    {
        if (c < 32 || invalid_chars.find(c) != std::string::npos)
            return false;
    }

    // check for reserved names (case-insensitive, with or without extension)
    std::string name_only = std::string(fn);
    size_t dot = fn.find('.');
    if (dot != std::string::npos)
        name_only = fn.substr(0, dot);

    std::string upper_name;
    upper_name.reserve(name_only.length());
    std::transform(name_only.begin(), name_only.end(), std::back_inserter(upper_name), ::toupper);

    return !reserved.count(upper_name);
}

//
// convert % to %% to avoid the use of formatting
//

std::string Melt::escPercent(std::string_view s)
{
    std::string ret{};
    
    for (char c : s)
    {
        if (c == '%')
            ret += "%%";
        else
            ret += c;
    }
    return ret;
}

//
// expand \t to 4 spaces (my personal preference, feel free to change it)
//

inline constexpr int tab_width = 4;
std::string Melt::expandTabs(std::string_view s)
{
    std::string ret{};

    int col = 0;
    for (char c : s)
    {
        if (c == '\t')
        {
            int spaces = tab_width - (col % tab_width);
            ret.append(spaces, ' ');
            col += spaces;
        }
        else
        {
            ret += c;
            ++col;
        }
    }
    return ret;
}

size_t Melt::visualLength(std::string_view s)
{
    size_t len = 0;
    for (char c : s)
    {
        if (c == '\t')
            len += tab_width - (len % tab_width);
        else
            ++len;
    }
    return len;
}
