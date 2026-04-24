#pragma once
#include "core.h"

class CubemapTexture
{
public:
	CubemapTexture();
	~CubemapTexture();

	// 从 HDR 等距矩形贴图加载, 自动转为立方体贴图
	bool loadFromHDR(const std::string& filepath);

	void bind(unsigned int textureUnit = 0) const;
	void unbind() const;

private:
	GLuint mTextureID{ 0 };
};
