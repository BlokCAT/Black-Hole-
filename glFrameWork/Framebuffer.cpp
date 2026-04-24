#include "Framebuffer.h"
#include "../wrapper/checkError.h"

Framebuffer::Framebuffer() {}

Framebuffer::~Framebuffer() {
    destroy();
}

void Framebuffer::create(int width, int height, bool useFloat) {
    mWidth = width;
    mHeight = height;

    // ---- 创建 FBO ----
    CK(glGenFramebuffers(1, &mFBO));
    CK(glBindFramebuffer(GL_FRAMEBUFFER, mFBO));

    // ---- 创建颜色附件纹理 ----
    CK(glGenTextures(1, &mColorTexture));
    CK(glBindTexture(GL_TEXTURE_2D, mColorTexture));

    if (useFloat) {
        // HDR：16位浮点，每个通道 16bit，可存储超过 1.0 的值
        CK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr));
    } else {
        // SDR：8位无符号
        CK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    }

    // 双线性过滤 + Clamp 到边缘（后处理必须，避免边缘漏光）
    CK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    CK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    CK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    CK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    // 将纹理绑定到 FBO 的颜色附件 0
    CK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColorTexture, 0));

    // ---- 创建深度/模板附件（RBO）----
    // 后处理不需要深度，但创建完整 FBO 避免状态问题
    CK(glGenRenderbuffers(1, &mDepthRBO));
    CK(glBindRenderbuffer(GL_RENDERBUFFER, mDepthRBO));
    CK(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height));
    CK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mDepthRBO));

    // ---- 检查完整性 ----
    GLenum status = CK(glCheckFramebufferStatus(GL_FRAMEBUFFER));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "[ERROR] Framebuffer 不完整! status = " << status << std::endl;
    }

    CK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void Framebuffer::destroy() {
    if (mColorTexture) { CK(glDeleteTextures(1, &mColorTexture)); mColorTexture = 0; }
    if (mDepthRBO)     { CK(glDeleteRenderbuffers(1, &mDepthRBO)); mDepthRBO = 0; }
    if (mFBO)          { CK(glDeleteFramebuffers(1, &mFBO)); mFBO = 0; }
    mWidth = mHeight = 0;
}

void Framebuffer::bind() const {
    CK(glBindFramebuffer(GL_FRAMEBUFFER, mFBO));
    CK(glViewport(0, 0, mWidth, mHeight));
}

void Framebuffer::unbind() const {
    CK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void Framebuffer::bindColorTexture(int textureUnit) const {
    CK(glActiveTexture(GL_TEXTURE0 + textureUnit));
    CK(glBindTexture(GL_TEXTURE_2D, mColorTexture));
}
