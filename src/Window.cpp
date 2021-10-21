#include <map>
#include <queue>
#include <thread>

#include <osdialog.h>

#include <window/Window.hpp>
#include <asset.hpp>
#include <widget/Widget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <patch.hpp>
#include <settings.hpp>
#include <plugin.hpp> // used in Window::screenshot
#include <system.hpp> // used in Window::screenshot

#ifdef NDEBUG
# undef DEBUG
#endif

#include "DistrhoUI.hpp"
#include "WindowParameters.hpp"

namespace rack {
namespace window {


static const math::Vec minWindowSize = math::Vec(640, 480);


void Font::loadFile(const std::string& filename, NVGcontext* vg) {
	this->vg = vg;
	handle = nvgCreateFont(vg, filename.c_str(), filename.c_str());
	if (handle < 0)
		throw Exception("Failed to load font %s", filename.c_str());
	INFO("Loaded font %s", filename.c_str());
}


Font::~Font() {
	// There is no NanoVG deleteFont() function yet, so do nothing
}


std::shared_ptr<Font> Font::load(const std::string& filename) {
	return APP->window->loadFont(filename);
}


void Image::loadFile(const std::string& filename, NVGcontext* vg) {
	this->vg = vg;
	handle = nvgCreateImage(vg, filename.c_str(), NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
	if (handle <= 0)
		throw Exception("Failed to load image %s", filename.c_str());
	INFO("Loaded image %s", filename.c_str());
}


Image::~Image() {
	// TODO What if handle is invalid?
	if (handle >= 0)
		nvgDeleteImage(vg, handle);
}


std::shared_ptr<Image> Image::load(const std::string& filename) {
	return APP->window->loadImage(filename);
}


struct WindowParams {
	float rackBrightness = 1.0f;
};

struct Window::Internal {
	DISTRHO_NAMESPACE::UI* ui = nullptr;
	DISTRHO_NAMESPACE::WindowParameters params;
	DISTRHO_NAMESPACE::WindowParametersCallback* callback = nullptr;

	math::Vec size = minWindowSize;
	std::string lastWindowTitle;

	int mods = 0;
	int frame = 0;
	int frameSwapInterval = 1;
	double monitorRefreshRate = 60.0; // FIXME
	double frameTime = 0.0;
	double lastFrameDuration = 0.0;

	std::map<std::string, std::shared_ptr<Font>> fontCache;
	std::map<std::string, std::shared_ptr<Image>> imageCache;

	bool fbDirtyOnSubpixelChange = true;
};

Window::Window() {
	internal = new Internal;
}

void WindowInit(Window* const window, DISTRHO_NAMESPACE::UI* const ui)
{
	const GLubyte* vendor = glGetString(GL_VENDOR);
	const GLubyte* renderer = glGetString(GL_RENDERER);
	const GLubyte* version = glGetString(GL_VERSION);
	INFO("Renderer: %s %s", vendor, renderer);
	INFO("OpenGL: %s", version);

	window->internal->ui = ui;

	window->vg = ui->getContext();
	window->fbVg = nvgCreateSharedGL2(window->vg, NVG_ANTIALIAS);

	// Load default Blendish font
	window->uiFont = window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
	if (window->uiFont != nullptr)
		bndSetFont(window->uiFont->handle);

	// Init settings
	WindowParametersRestore(window);

	if (APP->scene) {
		widget::Widget::ContextCreateEvent e;
		APP->scene->onContextCreate(e);
	}
}

void WindowMods(Window* const window, const int mods)
{
	window->internal->mods = mods;
}

Window::~Window() {
	if (APP->scene) {
		widget::Widget::ContextDestroyEvent e;
		APP->scene->onContextDestroy(e);
	}

	// Fonts and Images in the cache must be deleted before the NanoVG context is deleted
	internal->fontCache.clear();
	internal->imageCache.clear();

	delete internal;
}


math::Vec Window::getSize() {
	return internal->size;
}


void Window::setSize(math::Vec size) {
	internal->size = size.max(minWindowSize);
}


void Window::run() {
	internal->frame = 0;
}


void Window::step() {
	double frameTime = system::getTime();
	double lastFrameTime = internal->frameTime;
	internal->frameTime = frameTime;
	internal->lastFrameDuration = frameTime - lastFrameTime;
	// DEBUG("%.2lf Hz", 1.0 / internal->lastFrameDuration);

	// Make event handlers and step() have a clean NanoVG context
// 	nvgReset(vg);

	if (uiFont != nullptr)
		bndSetFont(uiFont->handle);

	// Set window title
	std::string windowTitle = APP_NAME + " " + APP_EDITION_NAME + " " + APP_VERSION;
	if (APP->patch->path != "") {
		windowTitle += " - ";
		if (!APP->history->isSaved())
			windowTitle += "*";
		windowTitle += system::getFilename(APP->patch->path);
	}
	if (windowTitle != internal->lastWindowTitle) {
		internal->ui->getWindow().setTitle(windowTitle.c_str());
		internal->lastWindowTitle = windowTitle;
	}

	// Get desired pixel ratio
	float newPixelRatio = internal->ui->getScaleFactor();
	if (newPixelRatio != pixelRatio) {
		pixelRatio = newPixelRatio;
		APP->event->handleDirty();
	}

	// Get framebuffer/window ratio
	int winWidth = internal->ui->getWidth();
	int winHeight = internal->ui->getHeight();
	int fbWidth = winWidth;// * newPixelRatio;
	int fbHeight = winHeight;// * newPixelRatio;
	windowRatio = (float)fbWidth / winWidth;

	if (APP->scene) {
		// DEBUG("%f %f %d %d", pixelRatio, windowRatio, fbWidth, winWidth);
		// Resize scene
		APP->scene->box.size = math::Vec(fbWidth, fbHeight).div(pixelRatio);

		// Step scene
		APP->scene->step();

		// Render scene
		// Update and render
		nvgScale(vg, pixelRatio, pixelRatio);

		// Draw scene
		widget::Widget::DrawArgs args;
		args.vg = vg;
		args.clipBox = APP->scene->box.zeroPos();
		APP->scene->draw(args);

		glViewport(0, 0, fbWidth, fbHeight);
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	internal->frame++;
}


void Window::screenshot(const std::string&) {
}


void Window::screenshotModules(const std::string&, float) {
}


void Window::close() {
	internal->ui->getWindow().close();
}


void Window::cursorLock() {
}


void Window::cursorUnlock() {
}


bool Window::isCursorLocked() {
	return false;
}


int Window::getMods() {
	return internal->mods;
}


void Window::setFullScreen(bool) {
}


bool Window::isFullScreen() {
	return false;
}


double Window::getMonitorRefreshRate() {
	return internal->monitorRefreshRate;
}


double Window::getFrameTime() {
	return internal->frameTime;
}


double Window::getLastFrameDuration() {
	return internal->lastFrameDuration;
}


double Window::getFrameDurationRemaining() {
	double frameDurationDesired = internal->frameSwapInterval / internal->monitorRefreshRate;
	return frameDurationDesired - (system::getTime() - internal->frameTime);
}


std::shared_ptr<Font> Window::loadFont(const std::string& filename) {
	const auto& pair = internal->fontCache.find(filename);
	if (pair != internal->fontCache.end())
		return pair->second;

	// Load font
	std::shared_ptr<Font> font;
	try {
		font = std::make_shared<Font>();
		font->loadFile(filename, vg);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		font = NULL;
	}
	internal->fontCache[filename] = font;
	return font;
}


std::shared_ptr<Image> Window::loadImage(const std::string& filename) {
	const auto& pair = internal->imageCache.find(filename);
	if (pair != internal->imageCache.end())
		return pair->second;

	// Load image
	std::shared_ptr<Image> image;
	try {
		image = std::make_shared<Image>();
		image->loadFile(filename, vg);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		image = NULL;
	}
	internal->imageCache[filename] = image;
	return image;
}


bool& Window::fbDirtyOnSubpixelChange() {
	return internal->fbDirtyOnSubpixelChange;
}


} // namespace window
} // namespace rack


START_NAMESPACE_DISTRHO

void WindowParametersSave(rack::window::Window* const window)
{
	if (d_isNotEqual(window->internal->params.cableOpacity, rack::settings::cableOpacity))
	{
		window->internal->params.cableOpacity = rack::settings::cableOpacity;
		if (window->internal->callback != nullptr)
			window->internal->callback->WindowParametersChanged(kWindowParameterCableOpacity,
			                                                    rack::settings::cableOpacity);
	}
	if (d_isNotEqual(window->internal->params.cableTension, rack::settings::cableTension))
	{
		window->internal->params.cableTension = rack::settings::cableTension;
		if (window->internal->callback != nullptr)
			window->internal->callback->WindowParametersChanged(kWindowParameterCableTension,
			                                                    rack::settings::cableTension);
	}
	if (d_isNotEqual(window->internal->params.rackBrightness, rack::settings::rackBrightness))
	{
		window->internal->params.rackBrightness = rack::settings::rackBrightness;
		if (window->internal->callback != nullptr)
			window->internal->callback->WindowParametersChanged(kWindowParameterRackBrightness,
			                                                    rack::settings::rackBrightness);
	}
	if (d_isNotEqual(window->internal->params.haloBrightness, rack::settings::haloBrightness))
	{
		window->internal->params.haloBrightness = rack::settings::haloBrightness;
		if (window->internal->callback != nullptr)
			window->internal->callback->WindowParametersChanged(kWindowParameterHaloBrightness,
			                                                    rack::settings::haloBrightness);
	}
}

void WindowParametersRestore(rack::window::Window* const window)
{
	rack::settings::cableOpacity = window->internal->params.cableOpacity;
	rack::settings::cableTension = window->internal->params.cableTension;
	rack::settings::rackBrightness = window->internal->params.rackBrightness;
	rack::settings::haloBrightness = window->internal->params.haloBrightness;
}

void WindowParametersSetCallback(rack::window::Window* const window, WindowParametersCallback* const callback)
{
	window->internal->callback = callback;
}

void WindowParametersSetValues(rack::window::Window* const window, const WindowParameters& params)
{
	window->internal->params = params;
}

END_NAMESPACE_DISTRHO
