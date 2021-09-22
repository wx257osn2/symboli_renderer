#include<symboli/prelude.hpp>
#include<optional>
#include<cstdint>
#include<nlohmann/json.hpp>
#include<fstream>
#include<symboli/il2cpp.hpp>
#include<iostream>

static std::optional<symboli::prelude> prelude;

struct config_t{
	int max_fps = -1;
	struct{
		float width = 16.f;
		float height = 9.f;
	}aspect_ratio;
	struct{
		bool enabled = false;
		float width = 1920.f;
		float height = 1080.f;
		float ui_scale = -1.f;
	}rendering_resolution;
	bool auto_full_screen = false;
	bool adjust_window_size = false;
	bool lock_window_size = false;
}static config;
static inline void from_json(const nlohmann::json& j, config_t& conf){
	auto config_opt_read = [](const nlohmann::json& j, const nlohmann::json::object_t::key_type& key, auto& value){
		prelude->config_read<true>("Symboli Renderer :: config_read", j, key, value);
	};
	config_opt_read(j, "max_fps", conf.max_fps);
	if(j.contains("aspect_ratio")){
		if(j["aspect_ratio"].is_object()){
			unsigned int width;
			j["aspect_ratio"].at("width").get_to(width);
			unsigned int height;
			j["aspect_ratio"].at("height").get_to(height);
			if(height > width)
				std::swap(width, height);
			conf.aspect_ratio.width = static_cast<float>(width);
			conf.aspect_ratio.height = static_cast<float>(height);
		}
		else if(j["aspect_ratio"].is_number()){
			float ratio;
			j["aspect_ratio"].get_to(ratio);
			if(ratio <= 0)
				throw std::runtime_error("aspect_ratio must be positive");
			if(ratio > 1.f){
				conf.aspect_ratio.width = ratio;
				conf.aspect_ratio.height = 1.f;
			}
			else{
				conf.aspect_ratio.width = 1.f;
				conf.aspect_ratio.height = ratio;
			}
		}
		else
			throw std::runtime_error("aspect_ratio must be {.width > 0, .height > 0} or number");
	}
	if(j.contains("rendering_resolution") && j["rendering_resolution"].is_object()){
		if(j["rendering_resolution"].contains("width") != j["rendering_resolution"].contains("height"))
			throw std::runtime_error("rendering_resolution.{width,height} must be defined together");
		unsigned int rendering_width = 1920u;
		config_opt_read(j["rendering_resolution"], "width", rendering_width);
		unsigned int rendering_height = 1080u;
		config_opt_read(j["rendering_resolution"], "height", rendering_height);
		if(rendering_width < rendering_height)
			std::swap(rendering_width, rendering_height);
		conf.rendering_resolution.width = static_cast<float>(rendering_width);
		conf.rendering_resolution.height = static_cast<float>(rendering_height);
		if(std::abs(conf.rendering_resolution.width/conf.aspect_ratio.width - conf.rendering_resolution.height/conf.aspect_ratio.height) >= 0.001f)
			prelude->diagnostic("Symboli Renderer :: config_read", "rendering_resolution.{width,height} doesn't match as aspect_ratio");
		config_opt_read(j["rendering_resolution"], "ui_scale", conf.rendering_resolution.ui_scale);
		conf.rendering_resolution.enabled = true;
	}
	config_opt_read(j, "auto_full_screen", conf.auto_full_screen);
	config_opt_read(j, "adjust_window_size", conf.adjust_window_size);
	config_opt_read(j, "lock_window_size", conf.lock_window_size);
}

struct set_target_framerate : symboli::hook_func<void(int), set_target_framerate>{
	static void func(int){
		orig(config.max_fps);
	}
};

static bool (*is_virt)();
static symboli::il2cpp::data_type::Resolution* (*get_current_resolution)(symboli::il2cpp::data_type::Resolution*);
static bool (*get_full_screen)();
static int (*get_width)();
static int (*get_height)();
static void (*set_scale_factor)(void*, float);

struct set_resolution : symboli::hook_func<void(int, int, bool), set_resolution>{
	static void func(int width, int height, bool full_screen){
		if(config.auto_full_screen){
			symboli::il2cpp::data_type::Resolution res;
			res = *get_current_resolution(&res);
			const bool display_virt = res.height > res.width;
			const bool window_virt = height > width;
			if(display_virt == window_virt){
				orig(res.width, res.height, true);
				return;
			}
		}
		orig(width, height, full_screen);
	}
};

struct get_optimized_window_size_virt : symboli::hook_func<symboli::il2cpp::data_type::UnityEngine::Vector3*(symboli::il2cpp::data_type::UnityEngine::Vector3*, int, int), get_optimized_window_size_virt>{
	static symboli::il2cpp::data_type::UnityEngine::Vector3* func(symboli::il2cpp::data_type::UnityEngine::Vector3* vec, int width, int height){
		auto ret = orig(vec, width, height);
		if(width != 0){
			if(is_virt())
				ret->y = width * config.aspect_ratio.width / config.aspect_ratio.height;
			else
				ret->y = width * config.aspect_ratio.height / config.aspect_ratio.width;
			ret->z = static_cast<float>(width)/height;
		}
		return ret;
	}
};

struct gallop_get_width : symboli::hook_func<int(), gallop_get_width>{
	static int func(){
		const auto ret = orig();
		if(config.auto_full_screen && get_full_screen()){
			symboli::il2cpp::data_type::Resolution res;
			res = *get_current_resolution(&res);
			return res.width;
		}
		if(config.rendering_resolution.enabled)
			return static_cast<int>(is_virt() ? config.rendering_resolution.height : config.rendering_resolution.width);
		return ret;
	}
};

struct gallop_get_height : symboli::hook_func<int(), gallop_get_height>{
	static int func(){
		const auto ret = orig();
		if(config.auto_full_screen && get_full_screen()){
			symboli::il2cpp::data_type::Resolution res;
			res = *get_current_resolution(&res);
			return res.height;
		}
		if(config.rendering_resolution.enabled)
			return static_cast<int>(is_virt() ? config.rendering_resolution.width : config.rendering_resolution.height);
		return ret;
	}
};

struct set_reference_resolution : symboli::hook_func<void(void*, symboli::il2cpp::data_type::UnityEngine::Vector2), set_reference_resolution>{
	static void func(void* self, symboli::il2cpp::data_type::UnityEngine::Vector2 res){
		if (config.rendering_resolution.enabled)
			if(config.rendering_resolution.ui_scale > 0.f)
				set_scale_factor(self, std::max(1.f, static_cast<float>(std::max(gallop_get_width::func(), gallop_get_height::func()))/1920.f) * config.rendering_resolution.ui_scale);
		if(config.auto_full_screen){
			symboli::il2cpp::data_type::Resolution r;
			r = *get_current_resolution(&r);
			const bool display_virt = r.height > r.width;
			const bool window_virt = res.y > res.x;
			if(display_virt == window_virt){
				res.x = static_cast<float>(r.width);
				res.y = static_cast<float>(r.height);
			}
		}
		else if(config.rendering_resolution.enabled){
			if(res.x < res.y)
				res.x = res.y*config.aspect_ratio.height/config.aspect_ratio.width;
			else
				res.x = res.y*config.aspect_ratio.width/config.aspect_ratio.height;
		}
		orig(self, res);
	}
};

struct wndproc : symboli::hook_func<LRESULT(HWND, UINT, WPARAM, LPARAM), wndproc>{
	static void change_window_size(HWND hwnd, int current_width, int current_height){
		static int last_width = current_width;
		static int last_height = current_height;
		const float ratio = is_virt() ? config.aspect_ratio.height/config.aspect_ratio.width : config.aspect_ratio.width/config.aspect_ratio.height;
		float height = static_cast<float>(current_height);
		float width = static_cast<float>(current_width);

		const auto new_ratio = width / height;

		if (new_ratio > ratio && (height >= last_height || width < last_width))
			height = width / ratio;
		else if (new_ratio < ratio && (width >= last_width || height < last_height))
			width = height * ratio;

		RECT win_rect;
		::GetWindowRect(hwnd, &win_rect);
		const int orig_win_width = win_rect.right - win_rect.left;
		const int orig_win_height = win_rect.bottom - win_rect.top;
		::MoveWindow(hwnd, win_rect.left, win_rect.top, static_cast<int>(orig_win_width - current_width + width), static_cast<int>(orig_win_height - current_height + height), FALSE);

		last_height = static_cast<int>(height);
		last_width = static_cast<int>(width);
	}
	static LRESULT func(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){
		static bool at_first = true;
		if(at_first && msg == WM_STYLECHANGED){
			at_first = false;
			RECT rect;
			::GetClientRect(hwnd, &rect);
			change_window_size(hwnd, rect.right, rect.bottom);
		}
		else if(msg == WM_STYLECHANGING && wparam == GWL_STYLE && config.lock_window_size)
			reinterpret_cast<STYLESTRUCT*>(lparam)->styleNew &= ~WS_THICKFRAME;
		else if(msg == WM_SIZE && wparam == 0){
			change_window_size(hwnd, LOWORD(lparam), HIWORD(lparam));
		}
		return orig(hwnd, msg, wparam, lparam);
	}
};

static inline BOOL process_attach(HINSTANCE hinst){
	const std::filesystem::path plugin_path{will::get_module_file_name(hinst).value()};
	prelude =+ symboli::prelude::create(plugin_path.parent_path()/"symboli_prelude.dll");

	std::ifstream config_file{(plugin_path.parent_path()/plugin_path.stem()).concat(".config.json")};
	if(config_file.is_open())try{
		nlohmann::json j;
		config_file >> j;
		config = j.get<config_t>();
		std::cout << "Symboli Renderer config: \n"
		             "  max_fps: " << config.max_fps << "\n"
		             "  aspect_ratio: {.width: " << config.aspect_ratio.width << ", .height: " << config.aspect_ratio.height << "}\n"
		             "  rendering_resolution: ";
		if(config.rendering_resolution.enabled)
			std::cout << "{.width: " << config.rendering_resolution.width << ", .height: " << config.rendering_resolution.height << ", .ui_scale: " << config.rendering_resolution.ui_scale << "}\n";
		else
			std::cout << "disabled\n";
		std::cout << "  auto_full_screen: " << config.auto_full_screen << "\n"
		             "  adjust_window_size: " << config.adjust_window_size << "\n"
		             "  lock_window_size: " << config.lock_window_size << std::endl;
	}catch(std::exception& e){
		::MessageBoxA(nullptr, e.what(), "Symboli Renderer exception", MB_OK|MB_ICONWARNING|MB_SETFOREGROUND);
	}

	prelude->enqueue_task([]{
		auto il2cpp =+ symboli::il2cpp::module::create(_T("GameAssembly.dll"));
		using symboli::il2cpp::get_method;
		using symboli::il2cpp::attached_thread;
		using symboli::il2cpp::data_type::UnityEngine::Vector2;
		using symboli::il2cpp::data_type::UnityEngine::Vector3;
		using symboli::il2cpp::data_type::Resolution;

		if(config.max_fps > 0){
			const auto set_targetFrameRate = il2cpp->*get_method<void(int)>(
				"UnityEngine.CoreModule.dll",
				"UnityEngine",
				"Application",
				"set_targetFrameRate");
			prelude->hook<set_target_framerate>(set_targetFrameRate).value();
		}

		is_virt = il2cpp->*get_method<bool()>(
			"umamusume.dll",
			"Gallop",
			"StandaloneWindowResize",
			"get_IsVirt");

		const auto getOptimizedWindowSizeVirt = il2cpp->*get_method<Vector3*(Vector3*, int, int)>(
			"umamusume.dll",
			"Gallop",
			"StandaloneWindowResize",
			"getOptimizedWindowSizeVirt",
			2);
		prelude->hook<get_optimized_window_size_virt>(getOptimizedWindowSizeVirt).value();

		if(config.rendering_resolution.enabled){
			set_scale_factor = il2cpp->*get_method<void(void*, float)>(
				"UnityEngine.UI.dll",
				"UnityEngine.UI",
				"CanvasScaler",
				"set_scaleFactor",
				1);
		}

		if(config.auto_full_screen){
			get_full_screen = il2cpp->*get_method<bool()>(
				"UnityEngine.CoreModule.dll",
				"UnityEngine",
				"Screen",
				"get_fullScreen");
		}

		if(config.auto_full_screen || config.adjust_window_size){
			get_current_resolution = il2cpp->*get_method<Resolution*(Resolution*)>(
				"UnityEngine.CoreModule.dll",
				"UnityEngine",
				"Screen",
				"get_currentResolution",
				0);
		}

		if(config.auto_full_screen || config.rendering_resolution.enabled){
			const auto get_Height = il2cpp->*get_method<int()>(
				"umamusume.dll",
				"Gallop",
				"Screen",
				"get_Height");
			prelude->hook<gallop_get_height>(get_Height).value();

			const auto get_Width = il2cpp->*get_method<int()>(
				"umamusume.dll",
				"Gallop",
				"Screen",
				"get_Width");
			prelude->hook<gallop_get_width>(get_Width).value();

			const auto set_referenceResolution = il2cpp->*get_method<void(void*, Vector2)>(
				"UnityEngine.UI.dll",
				"UnityEngine.UI",
				"CanvasScaler",
				"set_referenceResolution",
				1);
			prelude->hook<set_reference_resolution>(set_referenceResolution).value();
		}

		const auto WndProc = il2cpp->*get_method<LRESULT(HWND, UINT, WPARAM, LPARAM)>(
			"umamusume.dll",
			"Gallop",
			"StandaloneWindowResize",
			"WndProc"
			);
		prelude->hook<wndproc>(WndProc).value();

		const auto SetResolution = il2cpp->*get_method<void(int, int, bool)>(
			"UnityEngine.CoreModule.dll",
			"UnityEngine",
			"Screen",
			"SetResolution");
		prelude->hook<set_resolution>(SetResolution).value();

		if(config.adjust_window_size){
			(il2cpp->*attached_thread([]{
				symboli::il2cpp::data_type::Resolution res;
				get_current_resolution(&res);
				auto height = res.height - 100;
				set_resolution::orig(static_cast<int>(height * config.aspect_ratio.height / config.aspect_ratio.width), height, false);
			})).detach();
		}
	});
	return TRUE;
}

static inline BOOL process_detach(){
	if(prelude)
		prelude.reset();
	return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)try{
	switch(fdwReason){
	case DLL_PROCESS_ATTACH:
		return process_attach(hinstDLL);
	case DLL_PROCESS_DETACH:
		return process_detach();
	default:
		return TRUE;
	}
}catch(std::exception& e){
	::MessageBoxA(nullptr, e.what(), "Symboli Renderer exception", MB_OK|MB_ICONERROR|MB_SETFOREGROUND);
	return FALSE;
}
