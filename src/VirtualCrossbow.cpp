#include "VirtualCrossbow.h"
// TODO: move this to animationManager class
//  animations

VirtualCrossbow::VirtualCrossbow(int base, bool hand)
{
    prev_state = State::Start;
    _hand = hand;
    ammo = nullptr;
    grab_initialtheta = 0;
    grabAnim = false;
    higgs_palmPosHandspace = {0, -2.4, 6};
    config_InteractReloadDistance = 8;
    config_InteractAimDistance = 8;
    config_AllowAllActionsAlways = false;
    xbowStandard_Reload = {
        {"CrossBowBone_R01", {{{-0.06304830, 0, 0, 0.99801}, 0.0}, {{-0.157982, 0, 0, 0.987442}, 1.0}}},
        {"CrossBowBone_R02", {{{0.9966588, 0, 0, 0.0816776}, 0.0}, {{0.984319, 0, 0, 0.1763976}, 1.0}}},
        {"StringR", {{{0.144275, -0.0376906, 0.0054983, 0.9888043}, 0.0}, {{0.43479, -0.0375817, 0.0181629, 0.899564}, 1.0}}},
        {"CrossBowBone_L01", {{{0, 0.998009, -0.0630725, 0}, 0.0}, {{0, 0.987445, -0.157964, 0}, 1.0}}},
        {"CrossBowBone_L02", {{{0.996659, 0, 0, 0.0816809}, 0.0}, {{0.984326, 0, 0, 0.176359}, 1.0}}},
        {"StringL", {{{-0.0376816, 0.144283, 0.988804, 0.00549792}, 0.0}, {{-0.0375721, 0.434792, 0.899563, 0.0181594}, 1.0}}}};
    // CreateOverlapSpheres(OverlapSphereID_PlaceArrow);

    ReadStateFromExtraData();
    GotoState(State::Empty);
};

VirtualCrossbow::~VirtualCrossbow()
{
    VRCR::g_VRManager->DestroyLocalOverlapObject(OverlapSphereID_PlaceArrow);
    GotoState(State::End);
    WriteStateToExtraData();
};

void VirtualCrossbow::Update()
{
    using namespace RE;
    using namespace VRCR;
    if (grabAnim)
    {
        SKSE::log::info("xbow update");
        auto crossbowGrab = getGrabNode();
        auto crossbowRot = getLeverRotNode();
        auto weaponRoot = getThisWeaponNode();
        auto LHand = getThisHandNode();
        auto Lcontroller = getThisControllerNode();

        if (LHand && crossbowGrab && crossbowRot)
        {
            auto ctx = NiUpdateData();

            // update cocking lever angle
            NiPoint3 dvector = Lcontroller->world.translate - crossbowRot->world.translate;
            dvector = weaponRoot->world.Invert().rotate * dvector;
            float theta = atan2(-1.0 * dvector.z, dvector.y) - grab_initialtheta;
            const float maxrot = 0.6283185;
            const float minrot = 0.01745329;
            NiPoint3 rot;
            crossbowRot->local.rotate.ToEulerAnglesXYZ(rot);
            rot.x = std::clamp(theta, minrot, maxrot);
            crossbowRot->local.rotate.SetEulerAnglesXYZ(rot);
            crossbowRot->Update(ctx);

            float progress = std::clamp((theta - minrot) / (maxrot - minrot), 0.0f, 1.0f);

            // animate the rest of the crossbow bones
            for (auto bone : xbowStandard_Reload)
            {
                auto node = weaponRoot->GetObjectByName(bone.first)->AsNode();
                if (node)
                {
                    auto keys = bone.second;
                    auto prevFrame = keys.front();
                    // loop over keyframes in the animation, checking to see which two the time falls between
                    for (int i = 1; i < keys.size(); i++)
                    {
                        if (keys[i].second >= progress)
                        {
                            NiMatrix3 rotate = helper::slerpQuat(progress, prevFrame.first, keys[i].first);
                            node->local.rotate = rotate;
                            node->Update(ctx);

                            break;
                        }
                        prevFrame = keys[i];
                    }
                }
            }

            // set grab hand transform
            auto handTransform = LHand->world;
            auto palmPos = handTransform * higgs_palmPosHandspace;
            auto desiredTransform = weaponRoot->world;
            desiredTransform.translate += palmPos - crossbowGrab->world.translate;
            auto desiredTransformHandspace = handTransform.Invert() * desiredTransform;
            g_higgsInterface->SetGrabTransform(true, desiredTransformHandspace);
        }
    }
}

void VirtualCrossbow::OnGrabStart()
{
    using namespace RE;
    using namespace VRCR;

    // determine if grabbing hand is in position for aiming, reload, or other
    auto weaponNode = getThisWeaponNode();
    auto grabHand = getOtherHandNode();

    NiPoint3 weaponToHand = weaponNode->world.translate - grabHand->world.translate;
    NiPoint3 weaponToHandHandspace = weaponNode->world.Invert().rotate * weaponToHand;
    SKSE::log::info("crossbow grab");

    // TODO: check hand orientation in addition to relative position
    // hand above crossbow, reload

    if ((state == State::Empty || config_AllowAllActionsAlways) && weaponToHandHandspace.z > 1.f)
    {
        auto crossbowGrab = getGrabNode();
        auto crossbowRot = getLeverRotNode();
        auto grabController = getOtherControllerNode();
        if (crossbowGrab)
        {
            if (crossbowRot)
            {
                if (grabController)
                {
                    // SKSE::log::info("dist reload: {}", crossbowGrab->world.translate.GetDistance(Lcontroller->world.translate));
                    if (crossbowGrab->world.translate.GetDistance(grabController->world.translate) < config_InteractReloadDistance)
                    {
                        // finger curl - need VRIK? possibly later callback
                        SKSE::log::info("reload: override higgs config");
                        // save/modify higgs config values
                        VRCR::OverrideHiggsConfig();
                        SKSE::log::info("reload: calculating");
                        NiPoint3 dvector = grabController->world.translate - crossbowRot->world.translate;
                        dvector = weaponNode->world.Invert().rotate * dvector;
                        grab_initialtheta = atan2(-1.0 * dvector.z, dvector.y);

                        auto handTransform = grabHand->world;

                        auto palmPos = handTransform * higgs_palmPosHandspace;
                        auto desiredTransform = weaponNode->world;
                        desiredTransform.translate += palmPos - crossbowGrab->world.translate;
                        auto desiredTransformHandspace = handTransform.Invert() * desiredTransform;
                        SKSE::log::info("reload: setting transform");
                        g_higgsInterface->SetGrabTransform(true, desiredTransformHandspace);
                        grabAnim = true;
                    }
                    else
                    {
                        SKSE::log::info("too far away for reload grab");
                    }
                }
                else
                {
                    SKSE::log::info("left controller node not found");
                }
            }
            else
            {
                SKSE::log::info("right rot node not found");
            }
        }

        else
        {
            SKSE::log::info("right grab node not found");
        }
    }

    else if (state == State::Loaded || config_AllowAllActionsAlways) // hand below crossbow, aim
    {

        if (config_SavedAimGrabHandspace.scale > 0 &&
            grabHand->world.translate.GetDistance(weaponNode->world.translate + config_SavedAimGrabPosition) < config_InteractAimDistance)
        {
            g_higgsInterface->SetGrabTransform(true, config_SavedAimGrabHandspace);
        }
        // TODO: set fingers
    }
};

void VirtualCrossbow::OnGrabStop()
{
    grabAnim = false;
};

void VirtualCrossbow::OnOverlap(PapyrusVR::VROverlapEvent e, uint32_t id, PapyrusVR::VRDevice device)
{
    using namespace RE;
    switch (state)
    {
    case State::Cocked:
        if (id == OverlapSphereID_PlaceArrow)
        {
            auto heldRef = g_higgsInterface->GetGrabbedObject(!_hand);
            if (heldRef && heldRef->IsAmmo() && heldRef->As<TESAmmo>()->IsBolt())
            {
                ammo = heldRef->As<TESAmmo>();
                heldRef->DeleteThis();
            }
        }
    }
};

void VirtualCrossbow::OnAnimEvent()
{
    switch (state)
    {
    case State::Sheathed:
        // if event = un sheathe
        GotoState(prev_state);
    default:
        GotoState(State::Sheathed);
    }
};

void VirtualCrossbow::OnPrimaryButtonPress(const vr::VRControllerState_t *out)
{
    switch (state)
    {
    case State::Cocked:
        if (config_AllowAllActionsAlways)
        {
            FireDry();
        }
        break;
    case State::Loaded:
        Fire();
    }
};

void VirtualCrossbow::Fire()
{
    // play sound
    Fire::ArrowFromPoint(VRCR::g_player, getThisWeaponNode()->world, VRCR::g_player->GetEquippedObject(_hand)->As<RE::TESObjectWEAP>(), ammo);
    // queue up animation
};

void VirtualCrossbow::FireDry(){
    // play sound
    // queue up animation
};

void VirtualCrossbow::CreateOverlapSpheres(uint32_t &PlaceArrow)
{
    using namespace RE;
    using namespace VRCR;
    PapyrusVR::Matrix34 transform;
    NiTransform HeadToFeet;
    HeadToFeet.translate = g_player->GetVRNodeData()->PlayerWorldNode->world.translate - g_player->GetVRNodeData()->UprightHmdNode->world.translate;
    PapyrusVR::OpenVRUtils::CopyNiTrasformToMatrix34(&HeadToFeet, &transform);
    PlaceArrow = VRCR::g_VRManager->CreateLocalOverlapSphere(
        0.2f, &transform, _hand ? PapyrusVR::VRDevice_LeftController : PapyrusVR::VRDevice_RightController);
};

void VirtualCrossbow::GotoState(State newstate)
{
    OnExitState();
    prev_state = state;
    state = newstate;
    OnEnterState();
};

void VirtualCrossbow::Unsheathe()
{
    if (state == State::Sheathed)
    {
        SKSE::log::info("{} crossbow draw", _hand ? "left" : "right");
        GotoState(prev_state);
    }
};

void VirtualCrossbow::Sheathe()
{
    if (state != State::Sheathed)
    {
        GotoState(State::Sheathed);
        SKSE::log::info("{} crossbow sheathe", _hand ? "left" : "right");
    }
};

void VirtualCrossbow::WriteStateToExtraData(){

};

void VirtualCrossbow::ReadStateFromExtraData(){};

void VirtualCrossbow::OnEnterState()
{
    switch (state)
    {
    case State::Start:
        break;
    case State::Sheathed:
        break;
    case State::Empty:
        break;
    case State::Cocked:
        break;
    case State::Loaded:
        break;
    case State::End:
        break;
    }
};

void VirtualCrossbow::OnExitState()
{
    switch (state)
    {
    case State::Start:
        break;
    case State::Sheathed:
        break;
    case State::Empty:
        break;
    case State::Cocked:
        break;
    case State::Loaded:
        break;
    case State::End:
        break;
    }
};

RE::NiPointer<RE::NiNode> VirtualCrossbow::getThisHandNode()
{
    if (_hand)
    {
        return VRCR::g_player->GetVRNodeData()->NPCLHnd;
    }
    else
    {
        return VRCR::g_player->GetVRNodeData()->NPCRHnd;
    }
};
RE::NiPointer<RE::NiNode> VirtualCrossbow::getOtherHandNode()
{
    if (!_hand)
    {
        return VRCR::g_player->GetVRNodeData()->NPCLHnd;
    }
    else
    {
        return VRCR::g_player->GetVRNodeData()->NPCRHnd;
    }
};
RE::NiPointer<RE::NiNode> VirtualCrossbow::getThisControllerNode()
{
    if (_hand)
    {
        return VRCR::g_player->GetVRNodeData()->LeftValveIndexControllerNode;
    }
    else
    {
        return VRCR::g_player->GetVRNodeData()->RightValveIndexControllerNode;
    }
};
RE::NiPointer<RE::NiNode> VirtualCrossbow::getOtherControllerNode()
{
    if (!_hand)
    {
        return VRCR::g_player->GetVRNodeData()->LeftValveIndexControllerNode;
    }
    else
    {
        return VRCR::g_player->GetVRNodeData()->RightValveIndexControllerNode;
    }
};
RE::NiNode *VirtualCrossbow::getThisWeaponNode()
{
    if (_hand)
    {
        auto node = VRCR::g_player->GetNodeByName("SHIELD");
        if (node)
        {
            return node->AsNode();
        }
        else
        {
            SKSE::log::info("shield not found");
        }
    }
    else
    {
        auto node = VRCR::g_player->GetNodeByName("WEAPON");
        if (node)
        {
            return node->AsNode();
        }
        else
        {
            SKSE::log::info("weapon not found");
        }
    }
    return nullptr;
};
RE::NiNode *VirtualCrossbow::getGrabNode()
{
    auto node = getThisWeaponNode();
    if (node)
    {
        auto AVnode = node->GetObjectByName("GrabNode");
        if (AVnode)
        {
            return AVnode->AsNode();
        }
        else
        {
            return nullptr;
        }
    }
    else
    {
        return nullptr;
    }
};
RE::NiNode *VirtualCrossbow::getLeverRotNode()
{
    auto node = getThisWeaponNode();
    if (node)
    {
        auto AVnode = node->GetObjectByName("CockingMechanismCtrl");
        if (AVnode)
        {
            return AVnode->AsNode();
        }
        else
        {
            return nullptr;
        }
    }
    else
    {
        return nullptr;
    }
};