#pragma once
#include "core.h"

// ============================================================
// Framebuffer Object 封装
// 用于离屏渲染（HDR、后处理、Bloom 等）
// ============================================================
class Framebuffer {
public:
    Framebuffer();
    ~Framebuffer();

    // 创建指定尺寸的 FBO，useFloat=true 时使用 GL_RGBA16F（HDR）
    void create(int width, int height, bool useFloat = true);

    // 释放资源
    void destroy();

    // 绑定/解绑此 FBO 为渲染目标
    void bind() const;
    void unbind() const;

    // 绑定颜色附件到指定纹理单元（供 shader 采样）
    void bindColorTexture(int textureUnit) const;

    // 获取器
    GLuint getFBO() const { return mFBO; }
    GLuint getColorTexture() const { return mColorTexture; }
    int getWidth() const { return mWidth; }
    int getHeight() const { return mHeight; }

private:
    GLuint mFBO = 0;
    GLuint mColorTexture = 0;
    GLuint mDepthRBO = 0;
    int mWidth = 0;
    int mHeight = 0;
};
