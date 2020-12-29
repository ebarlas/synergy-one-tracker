#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include "led-matrix.h"
#include "graphics.h"

uint8_t readByte(std::istream &is) {
    char t;
    is.get(t);
    return (uint8_t) t;
}

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    operator bool() const {
        return r > 0 || g > 0 || b > 0;
    }

    void load(std::istream &is) {
        r = readByte(is);
        g = readByte(is);
        b = readByte(is);
    }

    void render(rgb_matrix::FrameCanvas *buffer, int x, int y) const {
        buffer->SetPixel(x, y, r, g, b);
    }
};

struct Frame {
    uint8_t width;
    uint8_t height;
    std::vector<Rgb> pixels;

    void load(std::istream &is) {
        width = readByte(is);
        height = readByte(is);
        pixels.resize(width * height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                pixels[y * width + x].load(is);
            }
        }
    }

    bool isSet(int x, int y) {
        return pixels[y * width + x];
    }

    void render(rgb_matrix::FrameCanvas *buffer, int left, int top) const {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int xBuf = left + x;
                int yBuf = top + y;
                if (xBuf >= 0 && xBuf < buffer->width() && yBuf >= 0 && yBuf < buffer->height()) {
                    pixels[y * width + x].render(buffer, xBuf, yBuf);
                }
            }
        }
    }
};

struct Sprite {
    uint8_t numFrames;
    std::vector<Frame> frames;

    void load(std::istream &is) {
        numFrames = readByte(is);
        frames.resize(numFrames);
        for (int i = 0; i < numFrames; i++) {
            frames[i].load(is);
        }
    }

    static Sprite load(const char *file) {
        Sprite sprite;
        std::ifstream is(file);
        sprite.load(is);
        is.close();
        return sprite;
    }
};

volatile bool interrupted = false;

void interruptHandler(int signo) {
    interrupted = true;
}

rgb_matrix::RGBMatrix::Options makeOptions() {
    rgb_matrix::RGBMatrix::Options options;
    options.rows = 16;
    options.cols = 32;
    options.chain_length = 1;
    options.parallel = 1;
    options.show_refresh_rate = false;
    options.brightness = 80;
    options.hardware_mapping = "regular";
    return options;
}

struct Point {
    int x;
    int y;
};

struct Pixel {
    int startTick;
    Point start;
    Point end;
};

struct Animation {
    virtual void init(rgb_matrix::FrameCanvas *buffer) = 0;
    virtual int sleep() = 0;
    virtual bool render(rgb_matrix::FrameCanvas *buffer) = 0;
};

struct SynergyAnimation : public Animation {
    enum class State {
        ASSEMBLE,
        PAUSE,
        SCROLL
    };

    int offset;
    int delay;
    int assembleSleep;
    int pauseSleep;
    int scrollSleep;

    std::vector<Pixel> pixels;

    State state;
    int tick;

    SynergyAnimation(Sprite &sprite, int offset, int delay, int assembleSleep, int pauseSleep, int scrollSleep)
            : offset(offset), delay(delay), assembleSleep(assembleSleep), pauseSleep(pauseSleep),
              scrollSleep(scrollSleep) {
        scanLeft(sprite.frames[0]);
        scanRight(sprite.frames[1]);
    }

    void scanLeft(Frame &frame) {
        int startTick = 0;
        for (int x = frame.width - 1; x >= 0; x--) {
            for (int y = 0; y < frame.height; y++) {
                if (frame.isSet(x, y)) {
                    pixels.push_back(Pixel{startTick, {x - offset, y}, {x, y}});
                    startTick += delay;
                }
            }
        }
    }

    void scanRight(Frame &frame) {
        int startTick = 0;
        for (int x = 0; x < frame.width; x++) {
            for (int y = frame.height - 1; y >= 0; y--) {
                if (frame.isSet(x, y)) {
                    pixels.push_back(Pixel{startTick, {x + offset, y}, {x, y}});
                    startTick += delay;
                }
            }
        }
    }

    void init(rgb_matrix::FrameCanvas *buffer) override {
        tick = 0;
        state = State::ASSEMBLE;
    }

    int sleep() override {
        if (state == State::ASSEMBLE) {
            return assembleSleep;
        } else if (state == State::PAUSE) {
            return pauseSleep;
        } else {
            return scrollSleep;
        }
    }

    void renderAssemble(rgb_matrix::FrameCanvas *buffer) {
        for (auto &p : pixels) {
            if (p.startTick <= tick) {
                auto diff = tick - p.startTick;
                int x;
                if (p.end.x > p.start.x) {
                    x = std::min(p.start.x + diff, p.end.x);
                } else {
                    x = std::max(p.start.x - diff, p.end.x);
                }
                buffer->SetPixel(x, p.start.y, 150, 150, 150);
            }
        }
    }

    bool renderScroll(rgb_matrix::FrameCanvas *buffer) {
        bool done = true;
        for (auto &p : pixels) {
            buffer->SetPixel(p.end.x - tick, p.start.y, 150, 150, 150);
        }
        return done;
    }

    bool render(rgb_matrix::FrameCanvas *buffer) override {
        if (state == State::ASSEMBLE) {
            renderAssemble(buffer);
            if (tick == pixels[pixels.size() - 1].startTick + offset) {
                tick = 0;
                state = State::PAUSE;
            } else {
                tick++;
            }
            return false;
        } else if(state == State::PAUSE) {
            renderScroll(buffer);
            state = State::SCROLL;
            return false;
        } else {
            renderScroll(buffer);
            if (tick == buffer->width()) {
                return true;
            }
            tick++;
            return false;
        }
    }
};

struct ScrollingMessage : Animation {
    Frame *frame;
    rgb_matrix::Font *font;
    rgb_matrix::Color *color;
    std::string message;
    int left;

    ScrollingMessage(Frame *frame, rgb_matrix::Font *font, rgb_matrix::Color *color, std::string message)
            : frame(frame), font(font), color(color), message(std::move(message)) {}

    void init(rgb_matrix::FrameCanvas *buffer) override {
        left = buffer->width();
    }

    int sleep() override {
        return 55;
    }

    int render(rgb_matrix::FrameCanvas *buffer, int x, int y) const {
        return rgb_matrix::DrawText(
                buffer,
                *font,
                x,
                y + font->baseline(),
                *color,
                nullptr,
                message.c_str(),
                0);
    }

    bool render(rgb_matrix::FrameCanvas *buffer) override {
        frame->render(buffer, left, 3);
        int length = render(buffer, left + frame->width + 2, 0);
        left--;
        return left + frame->width + 2 + length < 0;
    }
};

struct Coupon {
    std::string message;
    bool up;

    static Coupon load(std::istream &is) {
        std::string message;
        std::getline(is, message);
        return {message.substr(1), message[0] == '+'};
    }

    static std::vector<Coupon> load(const char *file) {
        std::vector<Coupon> vec;
        std::ifstream is(file);
        while (is.good()) {
            vec.push_back(load(is));
        }
        is.close();
        return vec;
    }
};

std::vector<ScrollingMessage> makeMessages(
        std::vector<Coupon> &coupons,
        rgb_matrix::Font &font,
        rgb_matrix::Color &color,
        Sprite &arrowsSprite) {
    std::vector<ScrollingMessage> messages;
    for (auto &coupon : coupons) {
        auto &frame = arrowsSprite.frames[coupon.up ? 0 : 1];
        messages.emplace_back(&frame, &font, &color, coupon.message);
    }
    return messages;
}

void renderLoop(std::vector<Animation *> &animations, rgb_matrix::RGBMatrix *canvas, rgb_matrix::FrameCanvas *buffer) {
    auto it = animations.begin();
    (*it)->init(buffer);
    while (!interrupted) {
        buffer->Clear();
        bool complete = (*it)->render(buffer);
        buffer = canvas->SwapOnVSync(buffer);
        if (complete) {
            it++;
            if (it == animations.end()) {
                break;
            }
            (*it)->init(buffer);
        }
        usleep((*it)->sleep() * 1000);
    }
}

int main(int argc, char *argv[]) {
    rgb_matrix::Font font;
    if (!font.LoadFont(argv[1])) {
        std::cerr << "Unable to load font: " << argv[1] << std::endl;
        return 1;
    }

    auto options = makeOptions();
    rgb_matrix::RuntimeOptions runtimeOptions;

    auto *canvas = rgb_matrix::RGBMatrix::CreateFromOptions(options, runtimeOptions);
    if (canvas == nullptr) {
        std::cerr << "Unable to create canvas" << std::endl;
        return 1;
    }

    auto synergy = Sprite::load("synergy.bin");
    auto arrows = Sprite::load("arrows.bin");

    rgb_matrix::Color color{150, 150, 150};

    signal(SIGTERM, interruptHandler);
    signal(SIGINT, interruptHandler);

    auto buffer = canvas->CreateFrameCanvas();

    SynergyAnimation sa{synergy, 20, 10, 5, 1000, 60};

    while (!interrupted) {
        std::vector<Animation *> animations;
        animations.push_back(&sa);

        std::vector<Coupon> coupons = Coupon::load(argv[2]);
        std::vector<ScrollingMessage> messages = makeMessages(coupons, font, color, arrows);
        std::for_each(messages.begin(), messages.end(), [&animations](ScrollingMessage &m) { animations.push_back(&m); });

        renderLoop(animations, canvas, buffer);
    }

    return 0;
}
