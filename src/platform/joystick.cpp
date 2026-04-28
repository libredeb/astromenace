/****************************************************************************

    AstroMenace
    Hardcore 3D space scroll-shooter with spaceship upgrade possibilities.
    Copyright (C) 2006-2025 Mikhail Kurinnoi, Viewizard


    AstroMenace is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    AstroMenace is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with AstroMenace. If not, see <https://www.gnu.org/licenses/>.


    Website: https://viewizard.com/
    Project: https://github.com/viewizard/astromenace
    E-mail: viewizard@viewizard.com

*****************************************************************************/

// TODO move to SDL_GetTicks() usage

// TODO add support for SDL_INIT_HAPTIC (force feedback)

// NOTE SDL2 also provide SDL_INIT_GAMECONTROLLER now

/*
At this time we don't provide menu options to choose joystick, first one (with index 0) will be used by default.
Which Joystick should be used, could be configured via config file, "JoystickNum" parameter.
*/

#include "../core/core.h"
#include "../config/config.h"
#include "platform.h"
#include "SDL2/SDL.h"

// NOTE switch to nested namespace definition (namespace A::B::C { ... }) (since C++17)
namespace viewizard {
namespace astromenace {

namespace {

SDL_Joystick *Joystick{nullptr};
int JoystickAxisX{0};
int JoystickAxisY{0};
int JoystickButtonsQuantity{0};
std::vector<bool> JoystickButtons{};

float JoystickCurrentTime{0.0f};
float JoystickTimeDelta{0.0f};

} // unnamed namespace


/*
 * Joystick (re)initialization.
 */
bool JoystickInit(float InitialTime)
{
    JoystickClose(); // since we could reinit joystick, close it first

    if (SDL_NumJoysticks() <= 0) {
        return false;
    }

    std::cout << "Found Joystick(s):\n";
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        std::cout << "Joystick Name " << i << ": " << SDL_JoystickNameForIndex(i) << "\n";
    }

    if (GameConfig().JoystickNum >= SDL_NumJoysticks()) {
        ChangeGameConfig().JoystickNum = 0;
    }

    Joystick = SDL_JoystickOpen(GameConfig().JoystickNum);

    if (Joystick) {
        std::cout << "Opened Joystick " << GameConfig().JoystickNum << "\n"
                  << "Joystick Name: " << SDL_JoystickNameForIndex(GameConfig().JoystickNum) << "\n"
                  << "Joystick Number of Axes: " << SDL_JoystickNumAxes(Joystick) << "\n"
                  << "Joystick Number of Buttons: " << SDL_JoystickNumButtons(Joystick) << "\n"
                  << "Joystick Number of Balls: " << SDL_JoystickNumBalls(Joystick) << "\n\n";
    } else {
        std::cerr << __func__ << "(): " << "SDL_JoystickOpen() failed: " << SDL_GetError() << "\n";
        std::cout << "Couldn't open Joystick " << GameConfig().JoystickNum << "\n\n";
        return false;
    }

    JoystickCurrentTime = InitialTime;
    JoystickTimeDelta = 0.0f;

    JoystickAxisX = SDL_JoystickGetAxis(Joystick, 0);
    JoystickAxisY = SDL_JoystickGetAxis(Joystick, 1);
    JoystickButtonsQuantity = SDL_JoystickNumButtons(Joystick);
    JoystickButtons.resize(JoystickButtonsQuantity, false);

    return true;
}

/*
 * Close joystick.
 */
void JoystickClose()
{
    if (!Joystick) {
        return;
    }

    if (SDL_JoystickGetAttached(Joystick)) {
        SDL_JoystickClose(Joystick);
    }

    Joystick = nullptr;
    JoystickButtons.clear();
}

/*
 * Check current joystick status (opened or not).
 */
bool isJoystickAvailable()
{
    return Joystick;
}

/*
 * Get current opened joystick buttons quantity.
 */
int GetJoystickButtonsQuantity()
{
    if (!Joystick) {
        return 0;
    }

    return JoystickButtonsQuantity;
}

/*
 * Set joystick button status.
 */
void SetJoystickButton(int ButtonNumber, bool ButtonStatus)
{
    // we may have config that was made previously with another joystick
    // don't change game's config, just ignore this buttons check
    if (!Joystick || ButtonNumber < 0 || ButtonNumber >= JoystickButtonsQuantity) {
        return;
    }

    JoystickButtons[ButtonNumber] = ButtonStatus;
}

/*
 * Get joystick button status.
 */
bool GetJoystickButton(int ButtonNumber)
{
    // we may have config that was made previously with another joystick
    // don't change game's config, just ignore this buttons check
    if (!Joystick || ButtonNumber < 0 || ButtonNumber >= JoystickButtonsQuantity) {
        return false;
    }

    return JoystickButtons[ButtonNumber];
}

/*
 * Emulate mouse movements.
 * Reads axes 0/1 (analog stick or d-pad-as-axes) and hat 0 (d-pad-as-hat) as fallback,
 * so the cursor moves correctly regardless of how the controller maps the d-pad.
 */
void JoystickEmulateMouseMovement(float Time)
{
    if (!Joystick) {
        return;
    }

    JoystickTimeDelta = Time - JoystickCurrentTime;
    JoystickCurrentTime = Time;

    int X = SDL_JoystickGetAxis(Joystick, 0);
    int Y = SDL_JoystickGetAxis(Joystick, 1);

    // JoystickDeadZone: [0, 10]
    // min/max joystick axis value: -32768/32767
    // we are using 3000 here, since dead zone should not be same small/big as min/max joystick axis values
    if (abs(X) < GameConfig().JoystickDeadZone * 3000) {
        X = 0;
    }
    if (abs(Y) < GameConfig().JoystickDeadZone * 3000) {
        Y = 0;
    }

    // If axes are idle and the controller has a hat (d-pad as hat, e.g. 8BitDo in X-Input mode),
    // use the hat to drive cursor movement at full deflection speed.
    if (X == 0 && Y == 0 && SDL_JoystickNumHats(Joystick) > 0) {
        Uint8 hat = SDL_JoystickGetHat(Joystick, 0);
        if (hat & SDL_HAT_LEFT)  X = -32767;
        if (hat & SDL_HAT_RIGHT) X =  32767;
        if (hat & SDL_HAT_UP)    Y = -32767;
        if (hat & SDL_HAT_DOWN)  Y =  32767;
    }

    if (JoystickAxisX != X || JoystickAxisY != Y) {
        JoystickAxisX = 0;
        JoystickAxisY = 0;

        // InternalWidth here, since during one second we should move cursor from left to right
        int Xsm{static_cast<int>(GameConfig().InternalWidth * (X / 32768.0f) * JoystickTimeDelta)};
        int Ysm{static_cast<int>(GameConfig().InternalWidth * (Y / 32768.0f) * JoystickTimeDelta)};

        vw_SetMousePosRel(Xsm, Ysm);
    }
}

/*
 * Get normalized movement axes for direct ship control.
 * Reads axes 0/1 with dead zone applied; falls back to hat 0 if both axes are idle.
 * Returns true and sets X/Y in range [-1, 1] when any direction is active.
 * This bypasses the cursor-accumulation pipeline, so the ship keeps moving even
 * when the virtual cursor is clamped at the viewport boundary.
 */
bool GetJoystickMovementAxes(float &X, float &Y)
{
    X = 0.0f;
    Y = 0.0f;

    if (!Joystick) {
        return false;
    }

    int rawX = SDL_JoystickGetAxis(Joystick, 0);
    int rawY = SDL_JoystickGetAxis(Joystick, 1);

    if (abs(rawX) < GameConfig().JoystickDeadZone * 3000) {
        rawX = 0;
    }
    if (abs(rawY) < GameConfig().JoystickDeadZone * 3000) {
        rawY = 0;
    }

    if (rawX == 0 && rawY == 0 && SDL_JoystickNumHats(Joystick) > 0) {
        Uint8 hat = SDL_JoystickGetHat(Joystick, 0);
        if (hat & SDL_HAT_LEFT)  rawX = -32767;
        if (hat & SDL_HAT_RIGHT) rawX =  32767;
        if (hat & SDL_HAT_UP)    rawY = -32767;
        if (hat & SDL_HAT_DOWN)  rawY =  32767;
    }

    if (rawX == 0 && rawY == 0) {
        return false;
    }

    X = rawX / 32767.0f;
    Y = rawY / 32767.0f;
    return true;
}

/*
 * Provide joystick button's name - "ButtonN", where N is number.
 */
std::string JoystickButtonName(int ButtonNum)
{
    if (ButtonNum < 0) {
        return "?";
    }

    // ButtonNum + 1, since buttons index start from 0
    return vw_GetText("Button") + std::to_string(ButtonNum + 1);
}

} // astromenace namespace
} // viewizard namespace
