#pragma once
#include "core.h"


class Shader 
{
public:
	Shader(const char* vertexPath, const char* fragmentPath);
	~Shader();

	void begin(); //��ʼʹ�����shader
	void end(); //����ʹ�����shader

	void setUniformFloat(const std::string& name, float value);

	void setUniformInt(const std::string& name, int value);

	void setUniformVec3(const std::string& name, glm::vec3 value);

	void setUniformMatrix4x4(const std::string& name, glm::mat4 value);

	void setUniformCubeMap(const std::string& name, unsigned int textureUnit);
	void setUniformTexture2D(const std::string& name, unsigned int textureUnit);
private:
	GLuint mProgram = { 0 };
	void checkShaderError(GLuint target, std::string type);
};