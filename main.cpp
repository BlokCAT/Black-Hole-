#include <iostream>
#include "glFrameWork/core.h"
#include "glFrameWork/shader.h"
#include "glFrameWork/geometry.h"
#include "glFrameWork/CubemapTexture.h"
#include "glFrameWork/Framebuffer.h"
#include "wrapper/checkError.h"
#include "application/Application.h"
#include "application/camera/perspectiveCamera.h"
#include "application/camera/trackBallCameraControl.h"
#include "application/camera/GameCameraControl.h"
#include "application/camera/orthographicCamera.h"
#include <GLFW/glfw3.h>

using namespace std;
using namespace glm;

// ===== 全局对象 =====
Shader* blackholeShader = nullptr;
Shader* brightnessExtractShader = nullptr;
Shader* bloomDownsampleShader = nullptr;
Shader* bloomUpsampleShader = nullptr;
Shader* tonemapShader = nullptr;
Shader* fxaaShader = nullptr;

Geometry* fullscreenQuad = nullptr;
CubemapTexture* environmentMap = nullptr;

// ---- 相机选择（取消注释切换）----
// 方案1：透视相机 + TrackBall（当前使用，shader光线追踪基于透视投影）
PerspectiveCamera* camera = nullptr;
	//TrackBallCameraControl* cameraControl = nullptr;
	GameCameraControl* cameraControl = nullptr;
// ===== 窗口配置 =====
int screenWidth = 1900;
int screenHeight = 1100;


// ===== Bloom 配置 =====
const int BLOOM_MIP_LEVELS = 8;        // 下采样级数（越多光晕越大）
float bloomThreshold = 0.6f;           // 亮度提取阈值
float bloomStrength = 0.09f;           // 最终 Bloom 合成强度
float bloomFilterRadius = 0.01f;       // 上采样滤波半径
float exposure = 1.0f;                 // 曝光值
float highlightThreshold = 5.6f;       // 高光压缩阈值（亮度>此值开始压缩）
float highlightKnee = 0.5f;            // 高光压缩强度（越小越强，0.1~2.0）
bool fxaaEnabled = false;                // FXAA 开关

// ===== 帧率统计 =====
float fpsTimer = 0.0f;
int frameCount = 0;
float lastTime = 0.0f;

// ===== 黑洞参数 =====
vec3 blackholePosition(0.0f, 0.0f, 0.0f);
float schwarzschildRadius = 2.0f;

// ===== FBO =====
Framebuffer* hdrFBO = nullptr;                      // HDR 场景渲染目标
Framebuffer* brightnessFBO = nullptr;               // 亮度提取结果
Framebuffer* bloomFBOs[BLOOM_MIP_LEVELS];            // 下采样 mip 链（也用于上采样）
Framebuffer* tonemapFBO = nullptr;                   // 色调映射结果（FXAA 输入）

// ===== 回调函数 =====
void OnResize(int width, int height) {
	CK(glViewport(0, 0, width, height));
	cout << "OnResize: " << width << "x" << height << endl;
}

void OnKey(int key, int action, int mods) {
	cameraControl->onKey(key, action, mods);

	// 按 B 键切换 Bloom 开关
	static bool bloomEnabled = true;
	if (key == GLFW_KEY_B && action == GLFW_PRESS) {
		bloomEnabled = !bloomEnabled;
		bloomStrength = bloomEnabled ? 0.15f : 0.0f;
		cout << "[Bloom] " << (bloomEnabled ? "ON" : "OFF") << endl;
	}
	// 按 上/下 调 Bloom 强度
	if (key == GLFW_KEY_UP && action == GLFW_PRESS) {
		bloomStrength =std:: min(bloomStrength + 0.02f, 1.0f);
		cout << "[Bloom Strength] " << bloomStrength << endl;
	}
	if (key == GLFW_KEY_DOWN && action == GLFW_PRESS) {
		bloomStrength = std::max(bloomStrength - 0.02f, 0.0f);
		cout << "[Bloom Strength] " << bloomStrength << endl;
	}
	// 按 F 键切换 FXAA
	if (key == GLFW_KEY_F && action == GLFW_PRESS) {
		fxaaEnabled = !fxaaEnabled;
		cout << "[FXAA] " << (fxaaEnabled ? "ON" : "OFF") << endl;
	}
}

void OnMouse(int button, int action, int mods) {
	double x, y;
	app->getCursorPosition(&x, &y);
	cameraControl->onMouse(button, action, x, y);
}

void OnCursor(double xpos, double ypos) {
	cameraControl->onCursor(xpos, ypos);
}

void OnScroll(double offset) {
	cameraControl->onScroll(offset);
}

// ===== 初始化 =====
void prepareCamera() {
	camera = new PerspectiveCamera(
		60.0f,
		(float)app->getWidth() / (float)app->getHeight(),
		0.1f,
		1000.0f
	);

	cameraControl = new GameCameraControl();
	cameraControl->setCamera(camera);
	cameraControl->setSensitivity(0.2f);

	// 初始 Roll 倾斜 10°（绕相机视线方向 forward 旋转）
	// 效果：地平线倾斜，像歪着头看黑洞
	float tiltAngle = 10.0f;
	vec3 forward = glm::normalize(-camera->mPosition);  // 相机看向原点方向
	auto mat = glm::rotate(glm::mat4(1.0f), glm::radians(tiltAngle), forward);
	camera->mUp = mat * glm::vec4(camera->mUp, 0.0f);
	camera->mRight = mat * glm::vec4(camera->mRight, 0.0f);
}

void prepareShader() {
	blackholeShader = new Shader(
		"assets/shader/blackhole.vert",
		"assets/shader/blackhole.frag"
	);

	// Bloom 后处理 shader 链
	brightnessExtractShader = new Shader(
		"assets/shader/blackhole.vert",
		"assets/shader/bloom_brightness_extract.frag"
	);
	bloomDownsampleShader = new Shader(
		"assets/shader/blackhole.vert",
		"assets/shader/bloom_downsample.frag"
	);
	bloomUpsampleShader = new Shader(
		"assets/shader/blackhole.vert",
		"assets/shader/bloom_upsample.frag"
	);
	tonemapShader = new Shader(
		"assets/shader/blackhole.vert",
		"assets/shader/tonemap_composite.frag"
	);

	// FXAA 抗锯齿 shader
	fxaaShader = new Shader(
		"assets/shader/blackhole.vert",
		"assets/shader/fxaa.frag"
	);
}

void prepareEnvironment() {
	environmentMap = new CubemapTexture();
	string hdrPath = "assets/hdr/www.hdr";

	if (!environmentMap->loadFromHDR(hdrPath)) {
		cout << "============================================" << endl;
		cout << "[WARNING] HDR 加载失败" << endl;
		cout << "  将使用程序化星空作为背景" << endl;
		cout << "============================================" << endl;
	}
}

void prepareGeometry() {
	fullscreenQuad = Geometry::createFullscreenQuad();
}

// ============================================================
// 创建所有 FBO
// 渲染管线需要：
//   1. hdrFBO：全分辨率，RGBA16F，存原始 HDR 场景
//   2. brightnessFBO：全分辨率，RGBA16F，存亮度提取结果
//   3. bloomFBOs[0..N-1]：逐级减半分辨率，RGBA16F
//      下采样时从 [i-1] 读写到 [i]
//      上采样时从 [i] 读，累加到 [i-1]
// ============================================================
void prepareFBOs() {
	int w = screenWidth;
	int h = screenHeight;

	// HDR 场景 FBO（全分辨率，浮点）
	hdrFBO = new Framebuffer();
	hdrFBO->create(w, h, true);

	// 亮度提取 FBO（全分辨率，浮点）
	brightnessFBO = new Framebuffer();
	brightnessFBO->create(w, h, true);

	// Bloom mip 链：每级分辨率减半
	for (int i = 0; i < BLOOM_MIP_LEVELS; i++) {
		w = std::max(w / 2, 1);
		h = std::max(h / 2, 1);
		bloomFBOs[i] = new Framebuffer();
		bloomFBOs[i]->create(w, h, true);
	}

	cout << "[FBO] HDR: " << screenWidth << "x" << screenHeight
	     << ", Bloom mip0: " << std::max(screenWidth/2,1) << "x" << std::max(screenHeight/2,1) << endl;

	// 色调映射结果 FBO（全分辨率，SDR，FXAA 的输入）
	// 注意：FXAA 必须在 LDR 空间工作，所以用 SDR（8位）即可
	tonemapFBO = new Framebuffer();
	tonemapFBO->create(screenWidth, screenHeight, false);
}

// ============================================================
// 绘制全屏四边形的辅助函数
// 绑定 VAO → 绘制 → 解绑，所有后处理 pass 都用这个
// ============================================================
void drawFullscreenQuad() {
	CK(glBindVertexArray(fullscreenQuad->getVao()));
	CK(glDrawElements(GL_TRIANGLES, fullscreenQuad->getIndicesCount(), GL_UNSIGNED_INT, 0));
	CK(glBindVertexArray(0));
}

// ============================================================
// 完整管线：
//   Pass 0: 渲染黑洞场景 → hdrFBO（HDR 颜色，无色调映射）
//   Pass 1: 亮度提取 hdrFBO → brightnessFBO
//   Pass 2: 下采样 brightnessFBO → bloomFBOs[0..N-1]
//   Pass 3: 上采样 bloomFBOs[N-1..0]，逐级累加
//   Pass 4: 色调映射 + 合成 → 屏幕
// ============================================================
void render() {
	// ========== Pass 0: 渲染黑洞场景到 HDR FBO ==========
	hdrFBO->bind();
	CK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

	mat4 view = camera->getViewMatrix();
	mat4 projection = camera->getProjectionMatrix();
	mat4 vp = projection * view;
	mat4 inverseVP_mat = inverse(vp);

	if (environmentMap) {
		environmentMap->bind(0);
	}

	blackholeShader->begin();
	blackholeShader->setUniformMatrix4x4("inverseVP", inverseVP_mat);
	blackholeShader->setUniformVec3("cameraPos", camera->mPosition);
	blackholeShader->setUniformVec3("blackholePos", blackholePosition);
	blackholeShader->setUniformFloat("schwarzschildRadius", schwarzschildRadius);
	blackholeShader->setUniformFloat("time", (float)glfwGetTime());
	blackholeShader->setUniformCubeMap("environmentMap", 0);

	drawFullscreenQuad();
	blackholeShader->end();
	hdrFBO->unbind();

	// ========== Pass 1: 亮度提取 ==========
	// 从 HDR 场景中提取亮度 > threshold 的像素
	brightnessFBO->bind();
	CK(glClear(GL_COLOR_BUFFER_BIT));

	brightnessExtractShader->begin();
	hdrFBO->bindColorTexture(0);  // 绑定 HDR 场景到纹理单元 0
	brightnessExtractShader->setUniformTexture2D("hdrTexture", 0);
	brightnessExtractShader->setUniformFloat("bloomThreshold", bloomThreshold);

	drawFullscreenQuad();
	brightnessExtractShader->end();
	brightnessFBO->unbind();

	// ========== Pass 2: 逐级下采样 ==========
	// brightnessFBO → bloomFBOs[0] → bloomFBOs[1] → ... → bloomFBOs[N-1]
	// 每次分辨率减半，同时做模糊（13-tap 滤波）
	bloomDownsampleShader->begin();

	// 第一级：从 brightnessFBO 下采样到 bloomFBOs[0]
	bloomFBOs[0]->bind();
	CK(glClear(GL_COLOR_BUFFER_BIT));
	brightnessFBO->bindColorTexture(0);
	bloomDownsampleShader->setUniformTexture2D("srcTexture", 0);
	bloomDownsampleShader->setUniformFloat("srcResolution", (float)screenWidth);
	drawFullscreenQuad();
	bloomFBOs[0]->unbind();

	// 后续级：从 bloomFBOs[i-1] 下采样到 bloomFBOs[i]
	for (int i = 1; i < BLOOM_MIP_LEVELS; i++) {
		bloomFBOs[i]->bind();
		CK(glClear(GL_COLOR_BUFFER_BIT));
		bloomFBOs[i-1]->bindColorTexture(0);
		bloomDownsampleShader->setUniformTexture2D("srcTexture", 0);
		// srcResolution 用上一级的宽度
		bloomDownsampleShader->setUniformFloat("srcResolution", (float)bloomFBOs[i-1]->getWidth());
		drawFullscreenQuad();
		bloomFBOs[i]->unbind();
	}

	bloomDownsampleShader->end();

	// ========== Pass 3: 逐级上采样 + 累加 ==========
	// 从最粗级 bloomFBOs[N-1] 开始，上采样累加到上一级
	// 简单方案：additive blending，上采样结果叠加到高分辨率 FBO
	bloomUpsampleShader->begin();
	bloomUpsampleShader->setUniformFloat("filterRadius", bloomFilterRadius);

	// 加法混合：上采样结果 += 已有的下采样结果
	CK(glEnable(GL_BLEND));
	CK(glBlendFunc(GL_ONE, GL_ONE));

	for (int i = BLOOM_MIP_LEVELS - 1; i > 0; i--) {
		bloomFBOs[i-1]->bind();

		bloomFBOs[i]->bindColorTexture(0);
		bloomUpsampleShader->setUniformTexture2D("srcTexture", 0);
		bloomUpsampleShader->setUniformFloat("srcResolution", (float)bloomFBOs[i]->getWidth());

		drawFullscreenQuad();
		bloomFBOs[i-1]->unbind();
	}

	CK(glDisable(GL_BLEND));
	bloomUpsampleShader->end();

	// ========== Pass 4: 色调映射 + Bloom 合成 ==========
	// FXAA 开启：输出到 tonemapFBO（LDR），后面 FXAA 从它读取
	// FXAA 关闭：直接输出到屏幕（FBO 0），省掉一次全屏 draw
	CK(glBindFramebuffer(GL_FRAMEBUFFER, fxaaEnabled ? tonemapFBO->getFBO() : 0));
	CK(glViewport(0, 0, screenWidth, screenHeight));
	CK(glClear(GL_COLOR_BUFFER_BIT));

	tonemapShader->begin();
	hdrFBO->bindColorTexture(0);          // 原始 HDR 场景
	bloomFBOs[0]->bindColorTexture(1);    // Bloom 结果（最细级 = 已累加完）
	tonemapShader->setUniformTexture2D("hdrTexture", 0);
	tonemapShader->setUniformTexture2D("bloomTexture", 1);
	tonemapShader->setUniformFloat("bloomStrength", bloomStrength);
	tonemapShader->setUniformFloat("exposure", exposure);
	tonemapShader->setUniformFloat("highlightThreshold", highlightThreshold);
	tonemapShader->setUniformFloat("highlightKnee", highlightKnee);

	drawFullscreenQuad();
	tonemapShader->end();

	if (fxaaEnabled) {
		tonemapFBO->unbind();
	}

	// ========== Pass 5: FXAA 抗锯齿（仅 FXAA 开启时执行）==========
	if (fxaaEnabled) {
		CK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
		CK(glViewport(0, 0, screenWidth, screenHeight));
		CK(glClear(GL_COLOR_BUFFER_BIT));

		fxaaShader->begin();
		tonemapFBO->bindColorTexture(0);
		fxaaShader->setUniformTexture2D("srcTexture", 0);
		fxaaShader->setUniformVec3("resolution", vec3((float)screenWidth, (float)screenHeight, 0.0f));
		drawFullscreenQuad();
		fxaaShader->end();
	}
}

// ===== 主函数 =====
int main() {
	if (!app->init(screenWidth, screenHeight)) {
		cout << "GLFW 初始化失败" << endl;
		return 0;
	}

	CK(glViewport(0, 0, screenWidth, screenHeight));
	CK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));

	// 注册回调
	app->setResizeCallback(OnResize);
	app->setKeyBoardCallback(OnKey);
	app->setMouseCallback(OnMouse);
	app->setCursorCallback(OnCursor);
	app->setScrollCallback(OnScroll);

	// 初始化各模块
	prepareCamera();
	prepareShader();
	prepareEnvironment();
	prepareGeometry();
	prepareFBOs();

	// 主循环
	lastTime = (float)glfwGetTime();
	while (app->update()) {
		float currentTime = (float)glfwGetTime();
		float deltaTime = currentTime - lastTime;
		lastTime = currentTime;

		fpsTimer += deltaTime;
		frameCount++;
		if (fpsTimer >= 1.0f) {
			float fps = frameCount / fpsTimer;
			float msPerFrame = (fpsTimer / frameCount) * 1000.0f;
			cout << "[FPS] " << fps << "  |  [Frame] " << msPerFrame << " ms" << endl;
			fpsTimer = 0.0f;
			frameCount = 0;
		}

		cameraControl->update();
		render();
	}

	// 清理（必须在 app->destroy() 之前！否则 GL 上下文已销毁，glDelete* 全部崩溃）
	delete blackholeShader;
	delete brightnessExtractShader;
	delete bloomDownsampleShader;
	delete bloomUpsampleShader;
	delete tonemapShader;
	delete fxaaShader;
	delete fullscreenQuad;
	delete environmentMap;
	delete camera;
	delete cameraControl;

	delete hdrFBO;
	delete brightnessFBO;
	delete tonemapFBO;
	for (int i = 0; i < BLOOM_MIP_LEVELS; i++) {
		delete bloomFBOs[i];
	}

	// 最后销毁窗口和 GL 上下文
	app->destroy();

	return 0;
}
