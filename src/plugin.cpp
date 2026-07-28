#include "log.h"
#include <unordered_set>
#include <SimpleIni.h>

static bool g_populateOnLoad = false;

namespace FastDualEquip
{
	inline std::unordered_set<RE::FormID> g_dualEquippedForms;

	class EquipEventHandler : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		static EquipEventHandler* GetSingleton()
		{
			static EquipEventHandler singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto actorRef = a_event->actor.get();
			if (!actorRef || actorRef != player) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Get what's currently held in right (slot 0) and left (slot 1) hands
			auto rightBound = player->GetEquippedObject(false);
			auto leftBound = player->GetEquippedObject(true);

			if (a_event->equipped) {
				auto formID = a_event->baseObject;
				auto form = RE::TESForm::LookupByID(formID);
				if (!form) {
					return RE::BSEventNotifyControl::kContinue;
				}

				// If both hands now hold the same FormID, add it to the set
				if (rightBound && leftBound && rightBound->GetFormID() == leftBound->GetFormID()) {
					g_dualEquippedForms.insert(rightBound->GetFormID());
				}

				// If an item in our record is equipped and not yet dual-equipped
				if (g_dualEquippedForms.contains(formID)) {

					bool isRightMatching = (rightBound && rightBound->GetFormID() == formID);
					bool isLeftMatching = (leftBound && leftBound->GetFormID() == formID);

					if ((isRightMatching && !isLeftMatching) || (!isRightMatching && isLeftMatching)) {
						if (auto* taskInterface = SKSE::GetTaskInterface()) {
							RE::FormID capturedFormID = formID;
							taskInterface->AddTask([capturedFormID]() {
								auto* deferredPlayer = RE::PlayerCharacter::GetSingleton();
								auto* deferredForm = RE::TESForm::LookupByID(capturedFormID);
								auto* equipManager = RE::ActorEquipManager::GetSingleton();
								if (!deferredPlayer || !deferredForm || !equipManager) {
									return;
								}

								// Re-check hand state
								auto currentRight = deferredPlayer->GetEquippedObject(false);
								auto currentLeft = deferredPlayer->GetEquippedObject(true);
								bool stillRightOnly = currentRight && currentRight->GetFormID() == capturedFormID &&
									!(currentLeft && currentLeft->GetFormID() == capturedFormID);
								bool stillLeftOnly = currentLeft && currentLeft->GetFormID() == capturedFormID &&
									!(currentRight && currentRight->GetFormID() == capturedFormID);
								if (!stillRightOnly && !stillLeftOnly) {
									return;
								}

								// Skyrim.esm slots : LeftHand = 0x13F42, RightHand = 0x13F43.
								static auto* leftSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x13F42);
								static auto* rightSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x13F43);

								if (auto spell = deferredForm->As<RE::SpellItem>()) {
									equipManager->EquipSpell(deferredPlayer, spell, leftSlot);
									equipManager->EquipSpell(deferredPlayer, spell, rightSlot);
								}
								else if (auto boundObj = deferredForm->As<RE::TESBoundObject>()) {
									equipManager->EquipObject(deferredPlayer, boundObj, nullptr, 1, leftSlot);
									equipManager->EquipObject(deferredPlayer, boundObj, nullptr, 1, rightSlot);
								}
							});
						}
					}
				}
			}
			else if (!rightBound && !leftBound)
			{
				g_dualEquippedForms.clear();
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};

	inline void Register()
	{
		if (auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink(EquipEventHandler::GetSingleton());
            SKSE::log::info("Dual Wield Memory event sink registered successfully!");
		}
	}
}

void OnDataLoaded()
{
	FastDualEquip::Register();
}

void PopulateFromFavorites()
{
	SKSE::GetTaskInterface()->AddTask([]() {
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		FastDualEquip::g_dualEquippedForms.clear();

		// --- Weapons
		auto inventory = player->GetInventory([](RE::TESBoundObject& a_obj) {
			return a_obj.Is(RE::FormType::Weapon);
		});

		for (auto& [item, entryData] : inventory) {
			auto& [count, entry] = entryData;
			if (count <= 0 || !entry) {
				continue;
			}

			if (entry->IsFavorited()) {
				FastDualEquip::g_dualEquippedForms.insert(item->GetFormID());
			}
		}

		// --- Spells
		if (auto* magicFavorites = RE::MagicFavorites::GetSingleton()) {
			for (auto* form : magicFavorites->spells) {
				if (form && form->Is(RE::FormType::Spell)) {
					FastDualEquip::g_dualEquippedForms.insert(form->GetFormID());
				}
			}
		}
	});
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		OnDataLoaded();
		break;
	case SKSE::MessagingInterface::kPostLoad:
		break;
	case SKSE::MessagingInterface::kPreLoadGame:
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
		if (g_populateOnLoad)
		{
			PopulateFromFavorites();
		}
		break;
	case SKSE::MessagingInterface::kNewGame:
		break;
	}
}

void LoadSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	const std::string path = std::string("Data/SKSE/Plugins/") + std::string(plugin->GetName()) + ".ini";
	ini.LoadFile(path.c_str());

	g_populateOnLoad = static_cast<bool>(ini.GetBoolValue("General", "bPopulateOnLoad", false));
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
	SKSE::Init(skse);
	SetupLog();

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	return true;
}