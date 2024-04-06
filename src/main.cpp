#include <Geode/Geode.hpp>

#include <ghc/filesystem.hpp>

#include <ctre.hpp>

using namespace geode::prelude;

// ported from https://github.com/matcool/small-gd-mods/blob/3e1783c7e281cbbccd53f9c4ceb697d5a6f839dd/src/menu-shaders.cpp

// most of this is from
// https://github.com/cocos2d/cocos2d-x/blob/5a25fe75cb8b26b61b14b070e757ec3b17ff7791/samples/Cpp/TestCpp/Classes/ShaderTest/ShaderTest.cpp
// and
// https://github.com/cgytrus/SimplePatchLoader/blob/752cf15eafd05a21031832f4dc847d78cd2cc5f7/src/pp.cpp

struct Shader {
    GLuint vertex = 0;
    GLuint fragment = 0;
    GLuint program = 0;

    Result<std::string> compile(
        std::string vertexSource,
        std::string fragmentSource
    ) {
        vertexSource = utils::string::trim(vertexSource);
        if (auto match = ctre::multiline_search<"^#version [0-9]+( core| compatibility|)$">(vertexSource)) {
            vertexSource.erase(match.get<0>().begin(), match.get<0>().end());
            log::warn("For shader developers: #version is unsupported! Always forced to 120 on Windows and undefined on macOS and mobile.");
        }
        if (auto match = ctre::multiline_search<"precision [a-zA-Z]+ [a-zA-Z]+;">(vertexSource)) {
            vertexSource.erase(match.get<0>().begin(), match.get<0>().end());
            log::warn("For shader developers: precision is unsupported! Always forced to undefined on desktop and highp on mobile.");
        }

        fragmentSource = utils::string::trim(fragmentSource);
        if (auto match = ctre::multiline_search<"^#version [0-9]+( core| compatibility|)$">(fragmentSource)) {
            fragmentSource.erase(match.get<0>().begin(), match.get<0>().end());
            log::warn("For shader developers: #version is unsupported! Always forced to 120 on Windows and undefined on macOS and mobile.");
        }
        if (auto match = ctre::multiline_search<"precision [a-zA-Z]+ [a-zA-Z]+;">(fragmentSource)) {
            fragmentSource.erase(match.get<0>().begin(), match.get<0>().end());
            log::warn("For shader developers: precision is unsupported! Always forced to undefined on desktop and highp on mobile.");
        }

        auto getShaderLog = [](GLuint id) -> std::string {
            GLint length, written;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
            if (length <= 0)
                return "";
            auto stuff = new char[length + 1];
            glGetShaderInfoLog(id, length, &written, stuff);
            std::string result(stuff);
            delete[] stuff;
            return result;
        };
        GLint res;

        vertex = glCreateShader(GL_VERTEX_SHADER);
        const char* vertexSources[] = {
#ifdef GEODE_IS_WINDOWS
            "#version 120\n",
#endif
#ifdef GEODE_IS_MOBILE
            "precision highp float;\n",
#endif
            vertexSource.c_str()
        };
        glShaderSource(vertex, sizeof(vertexSources) / sizeof(char*), vertexSources, nullptr);
        glCompileShader(vertex);
        auto vertexLog = getShaderLog(vertex);

        glGetShaderiv(vertex, GL_COMPILE_STATUS, &res);
        if(!res) {
            glDeleteShader(vertex);
            vertex = 0;
            return Err("vertex shader compilation failed:\n{}", vertexLog);
        }

        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        const char* fragmentSources[] = {
#ifdef GEODE_IS_WINDOWS
            "#version 120\n",
#endif
#ifdef GEODE_IS_MOBILE
            "precision highp float;\n",
#endif
            fragmentSource.c_str()
        };
        glShaderSource(fragment, sizeof(vertexSources) / sizeof(char*), fragmentSources, nullptr);
        glCompileShader(fragment);
        auto fragmentLog = getShaderLog(fragment);

        glGetShaderiv(fragment, GL_COMPILE_STATUS, &res);
        if(!res) {
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            vertex = 0;
            fragment = 0;
            return Err("fragment shader compilation failed:\n{}", fragmentLog);
        }

        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);

        return Ok(fmt::format(
            "shader compilation successful. logs:\nvert:\n{}\nfrag:\n{}",
            vertexLog, fragmentLog
        ));
    }

    Result<std::string> link() {
        if (!vertex)
            return Err("vertex shader not compiled");
        if (!fragment)
            return Err("fragment shader not compiled");

        auto getProgramLog = [](GLuint id) -> std::string {
            GLint length, written;
            glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
            if (length <= 0)
                return "";
            auto stuff = new char[length + 1];
            glGetProgramInfoLog(id, length, &written, stuff);
            std::string result(stuff);
            delete[] stuff;
            return result;
        };
        GLint res;

        glLinkProgram(program);
        auto programLog = getProgramLog(program);

        glDeleteShader(vertex);
        glDeleteShader(fragment);
        vertex = 0;
        fragment = 0;

        glGetProgramiv(program, GL_LINK_STATUS, &res);
        if(!res) {
            glDeleteProgram(program);
            program = 0;
            return Err("shader link failed:\n{}", programLog);
        }

        return Ok(fmt::format("shader link successful. log:\n{}", programLog));
    }

    void cleanup() {
        if (program)
            glDeleteProgram(program);
        program = 0;
    }
};

float shaderTime = 0.f;
class ShaderNode : public CCNode {
    Shader m_shader;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLint m_uniformResolution = 0;
    GLint m_uniformTime = 0;
    GLint m_uniformMouse = 0;
    GLint m_uniformPulse1 = 0;
    GLint m_uniformPulse2 = 0;
    GLint m_uniformPulse3 = 0;
    GLint m_uniformFft = 0;
    std::vector<std::tuple<std::string, CCNode*, GLint, GLint, GLint, GLint, GLint>> m_uniformNodes;
    FMOD::DSP* m_fftDsp = nullptr;
    static constexpr int FFT_SPECTRUM_SIZE = 1024;
    // gd cuts frequencies higher than ~16kHz, so we should too (the "140/512" part)
    // we also remove the right half (by multiplying by 2), because it's a mirrored version of the left half, so we don't need that
    // (and fmod actually removes it completely, so it's always all zeros anyway)
    // (there are actually 513 empty bins instead of 512 but the last one gets cut off by the "140/512" part)
    // i know, this is weird af
    static constexpr int FFT_ACTUAL_SPECTRUM_SIZE = FFT_SPECTRUM_SIZE - (FFT_SPECTRUM_SIZE * 140 / 512);
    static constexpr int FFT_WINDOW_SIZE = FFT_SPECTRUM_SIZE * 2;
    static constexpr float FFT_UPDATE_FREQUENCY = 20.f;
    float m_spectrum[FFT_ACTUAL_SPECTRUM_SIZE] { };
    float m_oldSpectrum[FFT_ACTUAL_SPECTRUM_SIZE] { };
    float m_newSpectrum[FFT_ACTUAL_SPECTRUM_SIZE] { };
    float m_spectrumUpdateAccumulator = 0.f;
    CCArrayExt<CCSprite*> m_shaderSprites;

public:
    ShaderNode() {
        for (int i = 0; i < FFT_ACTUAL_SPECTRUM_SIZE; ++i) {
            m_spectrum[i] = 0.f;
            m_oldSpectrum[i] = 0.f;
            m_newSpectrum[i] = 0.f;
        }
    }

    bool init(const std::string& vert, const std::string& frag) {
        this->setID("shader-background");

        auto res = m_shader.compile(vert, frag);
        if (!res) {
            log::error("{}", res.unwrapErr());
            return false;
        }
        log::info("{}", res.unwrap());

        glBindAttribLocation(m_shader.program, 0, "aPosition");

        res = m_shader.link();
        if (!res) {
            log::error("{}", res.unwrapErr());
            return false;
        }
        log::info("{}", res.unwrap());

        ccGLUseProgram(m_shader.program);

        m_shaderSprites.inner()->retain();
        std::istringstream stream(frag);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.starts_with("//@")) {
                line = utils::string::trim(line.substr(3));
                const auto addSprite = [&](const std::string& name) {
                    auto sprite = CCSprite::create(name.c_str());
                    sprite->retain();
                    m_shaderSprites.push_back(sprite);
                };
                std::string::size_type pos;
                while (pos = line.find(','), pos != std::string::npos) {
                    auto me = line.substr(0, pos);
                    line = line.substr(pos + 1);
                    addSprite(me);
                }
                if (!line.empty())
                    addSprite(line);
            }
            if (line.starts_with("//#")) {
                line = utils::string::trim(line.substr(3));
                size_t index = 0;
                const auto addNode = [&](const std::string& id) {
                    auto n = "node" + std::to_string(index++);
                    auto pos = glGetUniformLocation(m_shader.program, (n + "Pos").c_str());
                    auto rot = glGetUniformLocation(m_shader.program, (n + "Rot").c_str());
                    auto scale = glGetUniformLocation(m_shader.program, (n + "Scale").c_str());
                    auto size = glGetUniformLocation(m_shader.program, (n + "Size").c_str());
                    auto visible = glGetUniformLocation(m_shader.program, (n + "Visible").c_str());
                    m_uniformNodes.emplace_back(id, nullptr, pos, rot, scale, size, visible);
                };
                std::string::size_type pos;
                while (pos = line.find(','), pos != std::string::npos) {
                    auto me = line.substr(0, pos);
                    line = line.substr(pos + 1);
                    addNode(me);
                }
                if (!line.empty())
                    addNode(line);
            }
        }

        // this seems to not be needed
        //FMODAudioEngine::sharedEngine()->enableMetering();

        #ifndef GEODE_IS_MACOS // m_backgroundMusicChannel isn't in mac bindings
        auto engine = FMODAudioEngine::sharedEngine();
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_FFT, &m_fftDsp);
        engine->m_backgroundMusicChannel->addDSP(1, m_fftDsp);
        m_fftDsp->setParameterInt(FMOD_DSP_FFT_WINDOWTYPE, FMOD_DSP_FFT_WINDOW_HAMMING);
        m_fftDsp->setParameterInt(FMOD_DSP_FFT_WINDOWSIZE, FFT_WINDOW_SIZE);
        m_fftDsp->setActive(true);
        #endif

        GLfloat vertices[] = {
            // positions
            -1.0f, 1.0f,
            -1.0f, -1.0f,
            1.0f, -1.0f,

            -1.0f,  1.0f,
            1.0f, -1.0f,
            1.0f,  1.0f
        };
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void*)nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        m_uniformResolution = glGetUniformLocation(m_shader.program, "resolution");
        m_uniformTime = glGetUniformLocation(m_shader.program, "time");
        m_uniformMouse = glGetUniformLocation(m_shader.program, "mouse");
        m_uniformPulse1 = glGetUniformLocation(m_shader.program, "pulse1");
        m_uniformPulse2 = glGetUniformLocation(m_shader.program, "pulse2");
        m_uniformPulse3 = glGetUniformLocation(m_shader.program, "pulse3");
        m_uniformFft = glGetUniformLocation(m_shader.program, "fft");

        for (size_t i = 0; i < m_shaderSprites.size(); ++i) {
            auto uniform = glGetUniformLocation(m_shader.program, ("sprite" + std::to_string(i)).c_str());
            glUniform1i(uniform, (GLint)i);
        }

        this->scheduleUpdate();
        return true;
    }

    ~ShaderNode() override {
        if (m_fftDsp) {
            #ifndef GEODE_IS_MACOS
            FMODAudioEngine::sharedEngine()->m_backgroundMusicChannel->removeDSP(m_fftDsp);
            #endif
        }
    }

    void update(float dt) override {
        shaderTime += dt;
        m_spectrumUpdateAccumulator += dt;

        const float speed = 1.f / FFT_UPDATE_FREQUENCY;
        if (m_spectrumUpdateAccumulator >= speed) {
            if (m_fftDsp) {
                FMOD_DSP_PARAMETER_FFT* data;
                unsigned int length;
                m_fftDsp->getParameterData(FMOD_DSP_FFT_SPECTRUMDATA, (void**)&data, &length, nullptr, 0);
                if (length) {
                    for (size_t i = 0; i < std::min(data->length, FFT_ACTUAL_SPECTRUM_SIZE); i++) {
                        m_oldSpectrum[i] = m_newSpectrum[i];
                        m_newSpectrum[i] = 0.f;
                        int n = std::min(data->numchannels, 2);
                        for (size_t j = 0; j < n; ++j) {
                            m_newSpectrum[i] += data->spectrum[j][i];
                        }
                        m_newSpectrum[i] /= float(n);
                    }
                }
            }
            m_spectrumUpdateAccumulator = 0.f;
        }
        float t = m_spectrumUpdateAccumulator * FFT_UPDATE_FREQUENCY;
        for (int i = 0; i < FFT_ACTUAL_SPECTRUM_SIZE; i++) {
            m_spectrum[i] = (1.f - t) * m_oldSpectrum[i] + t * m_newSpectrum[i];
        }
    }

    static std::tuple<float, float, float, bool> getStuffRecursive(CCNode* node) {
        auto parent = node->getParent();
        if (!parent)
            return std::make_tuple(
                node->getRotation(),
                node->getScaleX(), node->getScaleY(),
                node->isVisible()
            );
        auto [parRot, parScaleX, parScaleY, parVis] = getStuffRecursive(parent);
        return std::make_tuple(
            parRot + node->getRotation(),
            parScaleX * node->getScaleX(), parScaleY * node->getScaleY(),
            parVis && node->isVisible()
        );
    }
    void draw() override {
        glBindVertexArray(m_vao);

        ccGLUseProgram(m_shader.program);

        auto glv = CCDirector::sharedDirector()->getOpenGLView();
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto frSize = glv->getFrameSize();

        glUniform2f(m_uniformResolution, frSize.width, frSize.height);
        auto mousePos = cocos::getMousePos() / winSize * frSize;
        glUniform2f(m_uniformMouse, mousePos.x, mousePos.y);

        for (size_t i = 0; i < m_shaderSprites.size(); ++i) {
            auto sprite = m_shaderSprites[i];
            ccGLBindTexture2DN(i, sprite->getTexture()->getName());
        }

        glUniform1f(m_uniformTime, shaderTime);

        // thx adaf for telling me where these are
        auto engine = FMODAudioEngine::sharedEngine();
        glUniform1f(m_uniformPulse1, /*engine->m_pulse1*/0.0f); // todo
        glUniform1f(m_uniformPulse2, /*engine->m_pulse2*/0.0f); // todo
        glUniform1f(m_uniformPulse3, /*engine->m_pulse3*/0.0f); // todo

        glUniform1fv(m_uniformFft, FFT_ACTUAL_SPECTRUM_SIZE, m_spectrum);

        for (auto& [id, node, posLoc, rotLoc, scaleLoc, sizeLoc, visibleLoc] : m_uniformNodes) {
            if (node == nullptr)
                node = this->getParent()->getChildByIDRecursive(id);
            if (node == nullptr) {
                log::warn("failed to find node with id '{}'", id);
                continue;
            }
            auto pos = node->convertToWorldSpaceAR(CCPoint{0.f, 0.f});
            glUniform2f(posLoc, pos.x, pos.y);
            glUniform2f(sizeLoc, node->getContentSize().width, node->getContentSize().height);
            if (!rotLoc && !scaleLoc && !visibleLoc)
                continue;
            auto [rotation, scaleX, scaleY, visible] = getStuffRecursive(node);
            glUniform1f(rotLoc, rotation);
            glUniform2f(scaleLoc, scaleX, scaleY);
            glUniform1i(visibleLoc, visible);
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);

#ifndef GEODE_IS_MACOS
        CC_INCREMENT_GL_DRAWS(1);
#endif
    }

    static ShaderNode* create(const std::string& vert, const std::string& frag) {
        auto node = new ShaderNode;
        if (!node->init(vert, frag)) {
            CC_SAFE_DELETE(node);
            return nullptr;
        }
        node->autorelease();
        return node;
    }

    static Result<ShaderNode*> createWithMenuName(const std::string& name) {
        ghc::filesystem::path vertexPath =
            (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename(Mod::get()->expandSpriteName((name + "-vert.glsl").c_str()), false);
        if (!ghc::filesystem::exists(vertexPath)) {
            vertexPath =
                (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename("any-vert.glsl"_spr, false);
        }

        ghc::filesystem::path fragmentPath =
            (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename(Mod::get()->expandSpriteName((name + "-frag.glsl").c_str()), false);
        if (!ghc::filesystem::exists(fragmentPath)) {
            fragmentPath =
                (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename("menu-shader.fsh", false);
        }
        if (!ghc::filesystem::exists(fragmentPath)) {
            fragmentPath =
                (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename("any-frag.glsl"_spr, false);
        }

        auto vertexSource = file::readString(vertexPath);
        if (!vertexSource)
            return Err("failed to read vertex shader at path {}: {}", vertexPath.string(),
                vertexSource.unwrapErr());

        auto fragmentSource = file::readString(fragmentPath);
        if (!fragmentSource)
            return Err("failed to read fragment shader at path {}: {}", fragmentPath.string(),
                vertexSource.unwrapErr());

        auto shader = ShaderNode::create(vertexSource.unwrap(), fragmentSource.unwrap());
        if (!shader)
            return Err("failed to create shader node");
        return Ok(shader);
    }

    static bool tryAddToNode(CCNode* node, const std::string& name, int zOrder) {
        if (!Mod::get()->getSettingValue<bool>("show-" + name)) {
            shaderTime = 0.f;
            return false;
        }

        auto res = ShaderNode::createWithMenuName(name);
        if (!res) {
            log::error("Failed to load menu shader: {}", res.unwrapErr());
            shaderTime = 0.f;
            return false;
        }
        auto shader = res.unwrap();
        shader->setZOrder(zOrder);
        node->addChild(shader);
        return true;
    }
};

#include <Geode/modify/MenuLayer.hpp>
class $modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init())
            return false;
        if (!ShaderNode::tryAddToNode(this, "main", -10))
            return true;
        for (const auto& child : CCArrayExt<CCNode*>(this->getChildren())) {
            auto layer = typeinfo_cast<MenuGameLayer*>(child);
            if (!layer)
                continue;
            layer->removeFromParentAndCleanup(true);
            break;
        }
        return true;
    }
};

#include <Geode/modify/LevelSelectLayer.hpp>
class $modify(LevelSelectLayer) {
    bool init(int lvl) {
        if (!LevelSelectLayer::init(lvl))
            return false;
        if (!ShaderNode::tryAddToNode(this, "level-select", -2))
            return true;
        for (const auto& child : CCArrayExt<CCNode*>(this->getChildren())) {
            auto sprite = typeinfo_cast<CCSprite*>(child);
            if (sprite && sprite->getZOrder() <= 1)
                sprite->setVisible(false);
            if (auto ground = typeinfo_cast<GJGroundLayer*>(child))
                ground->setVisible(false);
        }
        return true;
    }
};

#include <Geode/modify/CreatorLayer.hpp>
class $modify(CreatorLayer) {
    bool init() {
        if (!CreatorLayer::init())
            return false;
        if (!ShaderNode::tryAddToNode(this, "creator", -2))
            return true;
        this->getChildByID("background")->setVisible(false);
        for (const auto& child : CCArrayExt<CCNode*>(this->getChildren())) {
            auto sprite = typeinfo_cast<CCSprite*>(child);
            if (sprite && (sprite->getZOrder() == -1 || sprite->getZOrder() == 1))
                sprite->setVisible(false);
        }
        return true;
    }
};

#include <Geode/modify/LevelBrowserLayer.hpp>
class $modify(LevelBrowserLayer) {
    bool init(GJSearchObject* search) {
        if (!LevelBrowserLayer::init(search))
            return false;
        if (!ShaderNode::tryAddToNode(this, "level-browser", -2))
            return true;
        for (const auto& child : CCArrayExt<CCNode*>(this->getChildren())) {
            auto sprite = typeinfo_cast<CCSprite*>(child);
            if (sprite && (sprite->getZOrder() < 0 || sprite->getZOrder() == 1))
                sprite->setVisible(false);
        }
        return true;
    }
};

#include <Geode/modify/EditLevelLayer.hpp>
class $modify(EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level))
            return false;
        if (!ShaderNode::tryAddToNode(this, "edit-level", -2))
            return true;
        this->getChildByID("background")->setVisible(false);
        this->getChildByID("bottom-left-art")->setVisible(false);
        this->getChildByID("bottom-right-art")->setVisible(false);
        this->getChildByID("level-name-background")->setVisible(false);
        this->getChildByID("description-background")->setVisible(false);
        return true;
    }
};

#include <Geode/modify/LevelInfoLayer.hpp>
class $modify(LevelInfoLayer) {
    bool init(GJGameLevel* level, bool a) {
        if (!LevelInfoLayer::init(level, a))
            return false;
        if (!ShaderNode::tryAddToNode(this, "play-level", -2))
            return true;
        this->getChildByID("background")->setVisible(false);
        this->getChildByID("bottom-left-art")->setVisible(false);
        this->getChildByID("bottom-right-art")->setVisible(false);
        return true;
    }
};

#include <Geode/modify/LevelSearchLayer.hpp>
class $modify(LevelSearchLayer) {
    bool init(int a) {
        if (!LevelSearchLayer::init(a))
            return false;
        if (!ShaderNode::tryAddToNode(this, "search", -3))
            return true;
        this->getChildByID("background")->setVisible(false);
        this->getChildByID("left-corner")->setVisible(false);
        this->getChildByID("right-corner")->setVisible(false);
        this->getChildByID("level-search-bg")->setVisible(false);
        this->getChildByID("level-search-bar-bg")->setVisible(false);
        this->getChildByID("quick-search-bg")->setVisible(false);
        this->getChildByID("difficulty-filters-bg")->setVisible(false);
        this->getChildByID("length-filters-bg")->setVisible(false);
        return true;
    }
};
