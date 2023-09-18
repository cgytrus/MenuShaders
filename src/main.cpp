#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include <ghc/filesystem.hpp>

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
        const std::string& vertexSource,
        const std::string& fragmentSource
    ) {
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
        auto oglSucks = vertexSource.c_str();
        glShaderSource(vertex, 1, &oglSucks, nullptr);
        glCompileShader(vertex);
        auto vertexLog = getShaderLog(vertex);

        glGetShaderiv(vertex, GL_COMPILE_STATUS, &res);
        if(!res) {
            glDeleteShader(vertex);
            vertex = 0;
            return Err("vertex shader compilation failed:\n{}", vertexLog);
        }

        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        oglSucks = fragmentSource.c_str();
        glShaderSource(fragment, 1, &oglSucks, nullptr);
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
    float m_time = 0.f;
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
        auto res = m_shader.compile(vert, frag);
        if (!res) {
            log::error("{}", res.unwrapErr());
            return false;
        }
        log::info(res.unwrap());

        glBindAttribLocation(m_shader.program, 0, "aPosition");

        res = m_shader.link();
        if (!res) {
            log::error("{}", res.unwrapErr());
            return false;
        }
        log::info(res.unwrap());

        ccGLUseProgram(m_shader.program);

        m_shaderSprites.inner()->retain();
        if (frag.size() > 3 && frag.compare(0, 3, "//@") == 0) {
            std::istringstream stream(frag);
            stream.seekg(4);
            std::string line;
            std::getline(stream, line);
            std::string::size_type pos;
            const auto addSprite = [&](const std::string& name) {
                auto sprite = CCSprite::create(name.c_str());
                sprite->retain();
                m_shaderSprites.push_back(sprite);
            };
            while (pos = line.find(','), pos != std::string::npos) {
                auto me = line.substr(0, pos);
                line = line.substr(pos + 1);
                addSprite(me);
            }
            if (!line.empty()) addSprite(line);
        }

        auto engine = FMODAudioEngine::sharedEngine();
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_FFT, &m_fftDsp);
        engine->m_globalChannel->addDSP(1, m_fftDsp);
        m_fftDsp->setParameterInt(FMOD_DSP_FFT_WINDOWTYPE, FMOD_DSP_FFT_WINDOW_HAMMING);
        m_fftDsp->setParameterInt(FMOD_DSP_FFT_WINDOWSIZE, FFT_WINDOW_SIZE);
        m_fftDsp->setActive(true);

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
            FMODAudioEngine::sharedEngine()->m_globalChannel->removeDSP(m_fftDsp);
        }
    }

    void update(float dt) override {
        m_time += dt;
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

    void draw() override {
        glBindVertexArray(m_vao);

        ccGLEnable(m_eGLServerState);
        ccGLUseProgram(m_shader.program);

        auto glv = CCDirector::sharedDirector()->getOpenGLView();
        auto frSize = glv->getFrameSize();

        glUniform2f(m_uniformResolution, frSize.width, frSize.height);
        const auto mousePos = glv->getMousePosition();
        glUniform2f(m_uniformMouse, mousePos.x, frSize.height - mousePos.y);

        for (size_t i = 0; i < m_shaderSprites.size(); ++i) {
            auto sprite = m_shaderSprites[i];
            ccGLBindTexture2DN(i, sprite->getTexture()->getName());
        }

        glUniform1f(m_uniformTime, m_time);

        // thx adaf for telling me where these are
        auto engine = FMODAudioEngine::sharedEngine();
        glUniform1f(m_uniformPulse1, engine->m_pulse1);
        glUniform1f(m_uniformPulse2, engine->m_pulse2);
        glUniform1f(m_uniformPulse3, engine->m_pulse3);

        glUniform1fv(m_uniformFft, FFT_ACTUAL_SPECTRUM_SIZE, m_spectrum);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);
        CC_INCREMENT_GL_DRAWS(1);
    }

    static auto create(const std::string& vert, const std::string& frag) {
        auto node = new ShaderNode;
        if (node->init(vert, frag))
            node->autorelease();
        else
            CC_SAFE_DELETE(node);
        return node;
    }
};

class $modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init())
            return false;

        for (const auto& child : CCArrayExt<CCNode*>(this->getChildren())) {
            auto layer = typeinfo_cast<MenuGameLayer*>(child);
            if (!layer)
                continue;
            layer->removeFromParentAndCleanup(true);
            break;
        }

        ghc::filesystem::path vertexPath =
            (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename("menu-vert.glsl"_spr, false);

        ghc::filesystem::path fragmentPath =
            (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename("menu-shader.fsh", false);
        if (!ghc::filesystem::exists(fragmentPath)) {
            fragmentPath =
                (std::string)CCFileUtils::sharedFileUtils()->fullPathForFilename("menu-frag.glsl"_spr, false);
        }

        auto vertexSource = file::readString(vertexPath);
        if (!vertexSource) {
            log::error("failed to read vertex shader at path {}: {}", vertexPath.string(),
                vertexSource.unwrapErr());
            return true;
        }

        auto fragmentSource = file::readString(fragmentPath);
        if (!fragmentSource) {
            log::error("failed to read fragment shader at path {}: {}", fragmentPath.string(),
                vertexSource.unwrapErr());
            return true;
        }

        auto shader = ShaderNode::create(vertexSource.unwrap(), fragmentSource.unwrap());
        if (shader == nullptr)
            return true;

        shader->setZOrder(-10);
        this->addChild(shader);

        return true;
    }
};