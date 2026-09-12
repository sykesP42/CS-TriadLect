// 游戏内控制台 —— 单窗口里的中文 REPL（设计文档 §7.5）。
//
// 它同时是教学工具和调试工具，所以有两个刻意选择：
//   ① 中文输出 + ASCII 命令输入。中文给学生看，ASCII 给键盘省事 —— 不碰 IME，
//      就不会出现"输入法候选框和游戏窗口抢焦点"这种在宣讲现场没法收场的问题。
//   ② 面板只占屏幕下半部分，上方永远看得见世界。学生改一行数据、敲一条命令，
//      眼睛不用离开画面 —— 因果是当场看见的，不是看日志猜出来的。
//
// 这个文件只负责"终端"这件事：滚动缓冲、按像素折行、半透明面板、一行输入。
// 命令本身知道世界是什么，所以命令放在 main.cpp —— 这里的边界是"终端"和"世界"。
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../core/font.h"
#include "../core/framebuffer.h"

namespace dlab {

class Console {
public:
    enum class Tone {
        Info,   // 普通输出
        Echo,   // 你敲进去的那一行
        Ok,     // 成功（目标完成、命令生效）
        Error,  // 报错（数据文件里第几行错了、命令用法不对）
    };

    static constexpr int kMaxScrollback = 200;  // 最多留多少行，超了丢最老的
    static constexpr int kVisibleRows = 7;      // 面板里同时显示几行（含输入行）

    // 命令处理：整行原样交给外面（main.cpp 里那个知道 World 的地方）
    using Handler = std::function<void(const std::string&)>;

    void setHandler(Handler h) { handler_ = std::move(h); }

    // ---------------------------------------------------------------- 输出
    void print(const std::string& text) { push(text, Tone::Info); }
    void printOk(const std::string& text) { push(text, Tone::Ok); }
    void printError(const std::string& text) { push(text, Tone::Error); }
    void clear() { lines_.clear(); }

    int lineCount() const { return int(lines_.size()); }
    const std::string& lineAt(int i) const { return lines_[size_t(i)].text; }

    // ---------------------------------------------------------------- 输入
    // 只收 ASCII 可见字符（见文件头 ①）。超长直接截断，不给"输入缓冲区无限长"的机会。
    void typeChar(char c) {
        if (c >= 0x20 && c < 0x7F && input_.size() < 200) input_ += c;
    }
    void backspace() {
        if (!input_.empty()) input_.pop_back();
    }
    void submit() {
        if (input_.empty()) return;
        const std::string line = input_;
        input_.clear();
        run(line);
    }
    const std::string& input() const { return input_; }

    // 直接执行一行。离屏模式的 --cmd 注入走这条路径，和真人敲回车完全一样 ——
    // 所以"截图里能看到的"就是"宣讲现场按得出来的"。
    void run(const std::string& line) {
        push("> " + line, Tone::Echo);
        if (handler_) handler_(line);
    }

    // ---------------------------------------------------------------- 显示
    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    void toggle() { visible_ = !visible_; }

    static constexpr int kPadX = 10;
    static constexpr int kPadY = 6;

    // 面板实际占多高（像素）。贴底的 HUD 元素要躲开它 —— 否则会被半透明面板
    // 盖住，变成一层灰蒙蒙的鬼影，比直接看不见还难读。
    int panelHeight(const Font& font, int fbWidth) const {
        if (!visible_) return 0;
        const int rows = std::min<int>(int(countRows(font, fbWidth)), kVisibleRows - 1);
        return font.lineHeight() * (rows + 1) + kPadY * 2;
    }

    void draw(Framebuffer& fb, const Font& font) const {
        if (!visible_ || fb.width <= 0) return;

        const int lineH = font.lineHeight();
        const int maxW = fb.width - kPadX * 2;
        const std::vector<std::pair<std::string, Tone>> rows = wrapAll(font, maxW);
        const int histRows = std::min<int>(int(rows.size()), kVisibleRows - 1);

        const Vec3 panel{0.014f, 0.017f, 0.026f};
        const Vec3 edge{0.10f, 0.42f, 0.38f};
        const int panelH = lineH * (histRows + 1) + kPadY * 2;
        const int y0 = fb.height - panelH;

        for (int y = y0; y < fb.height; ++y)
            for (int x = 0; x < fb.width; ++x) fb.blendPixel(x, y, panel, 0.72f);
        for (int x = 0; x < fb.width; ++x) fb.blendPixel(x, y0, edge, 0.85f);  // 面板顶部亮线

        int y = y0 + kPadY;
        for (int i = 0; i < histRows; ++i) {
            const auto& r = rows[size_t(rows.size() - size_t(histRows) + size_t(i))];
            font.drawLine(fb, kPadX, y, r.first, toneColor(r.second));
            y += lineH;
        }

        // 输入行：太长了就只显示尾巴（跟真终端一样）
        std::string shown = input_;
        while (!shown.empty() && font.measureLine(shown) > maxW - font.measureLine("> ")) {
            size_t i = 0;
            utf8Next(shown, i);
            shown.erase(0, i);
        }
        const int promptW = font.drawLine(fb, kPadX, y, "> ", toneColor(Tone::Echo));
        const int inputW = font.drawLine(fb, kPadX + promptW, y, shown, toneColor(Tone::Info));
        // 光标一直亮着，不闪烁：闪烁会让同一帧的两次离屏截图不一致，
        // 而"逐像素比对"是这个项目验证渲染正确性的主要手段，不能自己给自己下绊子。
        rect(fb, kPadX + promptW + inputW + 1, y, 2, font.glyphH(), toneColor(Tone::Info), 1.0f);
    }

private:
    struct Line {
        std::string text;
        Tone tone = Tone::Info;
    };

    // 折行放在 draw 而不是 print：窗口尺寸一变，折行位置自己跟着变，
    // 不用在 resize 的时候去重排历史。200 行文本每帧折一次是微秒级的事。
    std::vector<std::pair<std::string, Tone>> wrapAll(const Font& font, int maxW) const {
        std::vector<std::pair<std::string, Tone>> rows;
        for (const Line& l : lines_)
            for (std::string& w : font.wrap(l.text, maxW)) rows.emplace_back(std::move(w), l.tone);
        return rows;
    }
    size_t countRows(const Font& font, int fbWidth) const {
        return wrapAll(font, fbWidth - kPadX * 2).size();
    }

    // 颜色都是线性 HDR 值，整张画面最后统一走 ACES + sRGB，所以"纯白"要给 2.2 左右
    static Vec3 toneColor(Tone t) {
        switch (t) {
            case Tone::Echo: return Vec3{0.55f, 1.55f, 1.30f};   // 青色，和终端屏幕同一个色系
            case Tone::Ok: return Vec3{2.60f, 2.00f, 0.75f};     // 琥珀色
            case Tone::Error: return Vec3{3.20f, 0.50f, 0.45f};
            default: return Vec3{2.20f, 2.20f, 2.25f};
        }
    }

    static void rect(Framebuffer& fb, int x, int y, int w, int h, Vec3 c, float alpha) {
        for (int gy = 0; gy < h; ++gy)
            for (int gx = 0; gx < w; ++gx) fb.blendPixel(x + gx, y + gy, c, alpha);
    }

    void push(const std::string& text, Tone tone) {
        lines_.push_back(Line{text, tone});
        if (int(lines_.size()) > kMaxScrollback) lines_.erase(lines_.begin());
    }

    std::vector<Line> lines_;
    std::string input_;
    Handler handler_;
    bool visible_ = false;
};

}  // namespace dlab
