// InputBench 0.1.0 - a test-only SKSE plugin that presses keys/buttons INSIDE the game's own
// input dispatch, so automated tests can drive ANY host (real SKSE Menu Framework included)
// without desktop-level SendInput, which Windows' foreground lock blocks for background
// processes. MIT, original code (the splice technique is this project's own, first shipped in
// the Apocrypha framework's amf.input on 2026-08-30).
//
// DevBench tool: POST /api/tool/bench.input {"op":"press","device":"keyboard|gamepad|mouse",
// "code":<DirectInput scan code / XInput mask / mouse button>,"hold":<frames>}.
// The press is spliced as RE::ButtonEvent nodes at the head of the dispatch list inside the
// PollInputDevices call the game itself makes - ControlMap, MenuControls and every mod's sink
// see it exactly as a hardware key. Down on the next dispatch, held for `hold` dispatches,
// then up. NOTE deliberately NOT used: BSInputEventQueue::GetSingleton() - its SE relocation
// returned garbage and crashed (2026-08-30).
#include "PCH.h"

#include "DevBench/DevBenchAPI.h"

#include <mutex>
#include <string>
#include <vector>

#include "utils/Logger.h"

namespace logger = SKSE::log;

namespace
{
	namespace offsets
	{
		// BSInputDeviceManager::PollInputDevices call site (SE 67315 / AE 68617 + 0x7B) - the
		// same pattern-guarded site the Apocrypha framework hooks for its menu input.
		inline constexpr REL::RelocationID kPollInputDevicesID{ 67315, 68617 };
		inline constexpr auto kPollInputDevicesOffset = REL::Offset(0x7B);
	}

	struct SynthPress
	{
		RE::INPUT_DEVICE device;
		std::uint32_t code;
		int framesLeft;
		bool downSent;
	};
	std::mutex g_synthLock;
	std::vector<SynthPress> g_synth;

	void SpliceButton(RE::InputEvent** a_events, RE::INPUT_DEVICE a_device, std::uint32_t a_code, float a_value, float a_held)
	{
		auto* controlMap = RE::ControlMap::GetSingleton();
		const std::string_view name = controlMap ? controlMap->GetUserEventName(a_code, a_device) : std::string_view{};
		RE::BSFixedString userEvent(name.empty() ? "" : std::string(name).c_str());
		auto* ev = RE::ButtonEvent::Create(a_device, userEvent, a_code, a_value, a_held);
		if (!ev)
		{
			logger::warn("bench.input: ButtonEvent::Create failed");
			return;
		}
		ev->next = *a_events;
		*a_events = ev;
		logger::info("bench.input: spliced {} {} code {} userEvent '{}'",
					 a_device == RE::INPUT_DEVICE::kGamepad ? "gamepad" : (a_device == RE::INPUT_DEVICE::kMouse ? "mouse" : "keyboard"),
					 a_value > 0.0F ? "DOWN" : "UP", a_code, name);
	}

	// Relative mouse movement, spliced like a button. CommonLibSSE has no MouseMoveEvent::Create,
	// so the event is built the way ButtonEvent::Create builds one: game-heap allocation, the
	// vtable emplaced, then the fields. A framework that integrates MouseMoveEvent deltas into
	// its own cursor (AMF does; the game recentres the OS cursor every frame) moves by exactly
	// (dx, dy) display pixels.
	void SpliceMouseMove(RE::InputEvent** a_events, std::int32_t a_dx, std::int32_t a_dy)
	{
		auto* ev = RE::malloc<RE::MouseMoveEvent>(sizeof(RE::MouseMoveEvent));
		if (!ev)
		{
			logger::warn("bench.input: MouseMoveEvent allocation failed");
			return;
		}
		std::memset(reinterpret_cast<void*>(ev), 0, sizeof(RE::MouseMoveEvent));
		RE::stl::emplace_vtable<RE::MouseMoveEvent>(ev);
		ev->device = RE::INPUT_DEVICE::kMouse;
		ev->eventType = RE::INPUT_EVENT_TYPE::kMouseMove;
		ev->next = nullptr;
		ev->userEvent = RE::BSFixedString("");
		ev->mouseInputX = a_dx;
		ev->mouseInputY = a_dy;
		ev->next = *a_events;
		*a_events = ev;
		logger::info("bench.input: spliced mouse move dx={} dy={}", a_dx, a_dy);
	}

	struct SynthMove { std::int32_t dx; std::int32_t dy; };
	std::vector<SynthMove> g_synthMoves;

	void ServiceSynthPresses(RE::InputEvent** a_events)
	{
		{
			std::vector<SynthMove> moves;
			{ std::scoped_lock lock(g_synthLock); moves.swap(g_synthMoves); }
			for (const auto& m : moves) { SpliceMouseMove(a_events, m.dx, m.dy); }
		}
		std::scoped_lock l(g_synthLock);
		for (auto it = g_synth.begin(); it != g_synth.end();)
		{
			if (!it->downSent) { SpliceButton(a_events, it->device, it->code, 1.0F, 0.0F); it->downSent = true; ++it; continue; }
			if (it->framesLeft-- > 0) { SpliceButton(a_events, it->device, it->code, 1.0F, 0.05F * static_cast<float>(it->framesLeft + 1)); ++it; continue; }
			SpliceButton(a_events, it->device, it->code, 0.0F, 0.1F);
			it = g_synth.erase(it);
		}
	}

	void QueuePress(RE::INPUT_DEVICE a_device, std::uint32_t a_code, int a_holdFrames)
	{
		std::scoped_lock l(g_synthLock);
		g_synth.push_back({ a_device, a_code, a_holdFrames < 1 ? 1 : (a_holdFrames > 600 ? 600 : a_holdFrames), false });
	}

	struct PollInputDevicesHook
	{
		static inline REL::Relocation<void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent**)> func;

		static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent** a_events)
		{
			if (a_events)
			{
				ServiceSynthPresses(a_events);
			}
			func(a_dispatcher, a_events);
		}
	};

	bool InstallHook()
	{
		const std::uintptr_t site = offsets::kPollInputDevicesID.address() + offsets::kPollInputDevicesOffset.offset();
		if (!REL::make_pattern<"E8">().match(site))
		{
			logger::error("PollInputDevices site 0x{:X} (ID {}+0x{:X}) is not a call instruction; bench.input NOT installed",
						  site, offsets::kPollInputDevicesID.id(), offsets::kPollInputDevicesOffset.offset());
			return false;
		}
		auto& trampoline = SKSE::GetTrampoline();
		PollInputDevicesHook::func = trampoline.write_call<5>(site, PollInputDevicesHook::thunk);
		logger::info("PollInputDevices hook installed (ID {}+0x{:X}); bench.input live",
					 offsets::kPollInputDevicesID.id(), offsets::kPollInputDevicesOffset.offset());
		return true;
	}

	// ---- minimal JSON pulls (flat args object; same approach as the framework's tools) ----
	std::string JsonStr(const std::string& a_json, const std::string& a_key)
	{
		const std::string pat = "\"" + a_key + "\"";
		auto p = a_json.find(pat);
		if (p == std::string::npos) { return {}; }
		p = a_json.find(':', p + pat.size());
		if (p == std::string::npos) { return {}; }
		p = a_json.find_first_not_of(" \t", p + 1);
		if (p == std::string::npos || a_json[p] != '"') { return {}; }
		const auto e = a_json.find('"', p + 1);
		return e == std::string::npos ? std::string{} : a_json.substr(p + 1, e - p - 1);
	}

	long JsonNum(const std::string& a_json, const std::string& a_key, long a_default)
	{
		const std::string pat = "\"" + a_key + "\"";
		auto p = a_json.find(pat);
		if (p == std::string::npos) { return a_default; }
		p = a_json.find(':', p + pat.size());
		if (p == std::string::npos) { return a_default; }
		try { return std::stol(a_json.substr(p + 1)); } catch (...) { return a_default; }
	}

	void InputTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
	{
		const std::string args = a_argsJson ? a_argsJson : "{}";
		const std::string op = JsonStr(args, "op");
		const std::string deviceName = JsonStr(args, "device");
		const long code = JsonNum(args, "code", -1);
		const long hold = JsonNum(args, "hold", 4);
		RE::INPUT_DEVICE device = RE::INPUT_DEVICE::kKeyboard;
		if (deviceName == "gamepad") { device = RE::INPUT_DEVICE::kGamepad; }
		else if (deviceName == "mouse") { device = RE::INPUT_DEVICE::kMouse; }
		else if (!deviceName.empty() && deviceName != "keyboard")
		{
			a_write(a_sink, "{\"ok\":false,\"error\":\"device must be keyboard|gamepad|mouse\"}");
			return;
		}
		if (op == "move")
		{
			const long dx = JsonNum(args, "dx", 0);
			const long dy = JsonNum(args, "dy", 0);
			{ std::scoped_lock lock(g_synthLock); g_synthMoves.push_back(SynthMove{ static_cast<std::int32_t>(dx), static_cast<std::int32_t>(dy) }); }
			const std::string reply = "{\"ok\":true,\"op\":\"move\",\"dx\":" + std::to_string(dx) + ",\"dy\":" + std::to_string(dy) + ",\"queued\":true}";
			a_write(a_sink, reply.c_str());
			return;
		}
		if (op != "press" || code < 0)
		{
			a_write(a_sink, "{\"ok\":false,\"error\":\"op must be press with a non-negative code\"}");
			return;
		}
		const auto c = static_cast<std::uint32_t>(code);
		const int frames = static_cast<int>(hold < 1 ? 1 : (hold > 600 ? 600 : hold));
		QueuePress(device, c, frames);
		const std::string reply = "{\"ok\":true,\"op\":\"press\",\"device\":\"" + (deviceName.empty() ? std::string("keyboard") : deviceName) +
								  "\",\"code\":" + std::to_string(c) + ",\"holdFrames\":" + std::to_string(frames) + ",\"queued\":true}";
		a_write(a_sink, reply.c_str());
	}

	void RegisterWithDevBench(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }
		auto* dev = DevBenchAPI::GetDevBenchInterface001();
		if (!dev)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; bench.input unavailable this session"); }
			return;
		}
		constexpr const char* descriptor =
			"{"
			"\"description\":\"Test-only: press a key/button INSIDE the game's input dispatch (works on any "
			"menu-framework host; no desktop focus needed). op: press; device: keyboard|gamepad|mouse; "
			"code: DirectInput scan code / XInput mask / mouse button; hold: frames.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"op\":{\"type\":\"string\"},\"device\":{\"type\":\"string\"},"
			"\"code\":{\"type\":\"number\"},\"hold\":{\"type\":\"number\"}}},"
			"\"readOnly\":false"
			"}";
		if (dev->RegisterTool("bench.input", descriptor, &InputTool, nullptr))
		{
			logger::info("Registered \"bench.input\" with DevBench (build {})", dev->GetBuildNumber());
		}
		registered = true;
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kInputLoaded:
			InstallHook();
			RegisterWithDevBench(false);
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			RegisterWithDevBench(true);
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::init("InputBench");
	logger::info("InputBench {} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));
	SKSE::AllocTrampoline(14);
	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);
	return true;
}
