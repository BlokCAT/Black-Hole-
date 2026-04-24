#include "CubemapTexture.h"
#include "../wrapper/checkError.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>

#define STBI_WINDOWS_UTF8
#include "../application/stb_image.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

CubemapTexture::CubemapTexture() {}
CubemapTexture::~CubemapTexture() {
	if (mTextureID != 0) {
		glDeleteTextures(1, &mTextureID);
	}
}

// 将立方体某个面的像素坐标转换为3D方向向量
static glm::vec3 faceToDirection(int face, float u, float v) {
	glm::vec3 dir;
	switch (face) {
	case 0: dir = glm::vec3( 1.0f, -v, -u); break; // +X (Right)
	case 1: dir = glm::vec3(-1.0f, -v,  u); break; // -X (Left)
	case 2: dir = glm::vec3( u,  1.0f,  v); break; // +Y (Top)
	case 3: dir = glm::vec3( u, -1.0f, -v); break; // -Y (Bottom)
	case 4: dir = glm::vec3( u, -v,  1.0f); break; // +Z (Front)
	case 5: dir = glm::vec3(-u, -v, -1.0f); break; // -Z (Back)
	}
	return glm::normalize(dir);
}

bool CubemapTexture::loadFromHDR(const std::string& filepath) {
	// 1. 加载 HDR 等距矩形图像
	int width, height, channels;
	float* hdrData = stbi_loadf(filepath.c_str(), &width, &height, &channels, 3);
	if (!hdrData) {
		std::cout << "  HDR 加载失败原因: " << stbi_failure_reason() << std::endl;
		return false;
	}


	// 2. 将等距矩形贴图转为6个立方体面
	int faceSize = 512;
	std::vector<float> faceData(faceSize * faceSize * 3);

	CK(glGenTextures(1, &mTextureID));
	CK(glBindTexture(GL_TEXTURE_CUBE_MAP, mTextureID));

	for (int face = 0; face < 6; face++) {
		for (int y = 0; y < faceSize; y++) {
			for (int x = 0; x < faceSize; x++) {
				// 像素坐标映射到 [-1, 1]
				float u = (2.0f * (x + 0.5f) / faceSize) - 1.0f;
				float v = (2.0f * (y + 0.5f) / faceSize) - 1.0f;

				// 转为3D方向
				glm::vec3 dir = faceToDirection(face, u, v);

				// 3D方向转为等距矩形UV
				float theta = atan2f(dir.x, dir.z);
				float phi = asinf(glm::clamp(dir.y, -1.0f, 1.0f));

				float eqU = theta / (2.0f * (float)M_PI) + 0.5f;
				float eqV = phi / (float)M_PI + 0.5f;

				// 采样HDR数据
				int sx = std::max(0, std::min((int)(eqU * width), width - 1));
				int sy = std::max(0, std::min((int)((1.0f - eqV) * height), height - 1));

				int dstIdx = (y * faceSize + x) * 3;
				int srcIdx = (sy * width + sx) * 3;

				faceData[dstIdx + 0] = hdrData[srcIdx + 0] *0.5;
				faceData[dstIdx + 1] = hdrData[srcIdx + 1] * 0.5;
				faceData[dstIdx + 2] = hdrData[srcIdx + 2] * 0.5;
			}
		}

		// 上传面数据到GPU
		GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + face;
		CK(glTexImage2D(target, 0, GL_RGB16F, faceSize, faceSize, 0, GL_RGB, GL_FLOAT, faceData.data()));
	}

	// 设置纹理参数
	CK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	CK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	CK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	CK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	CK(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

	CK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));

	stbi_image_free(hdrData);
	return true;
}

void CubemapTexture::bind(unsigned int textureUnit) const {
	CK(glActiveTexture(GL_TEXTURE0 + textureUnit));
	CK(glBindTexture(GL_TEXTURE_CUBE_MAP, mTextureID));
}

void CubemapTexture::unbind() const {
	CK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
}
