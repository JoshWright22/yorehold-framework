#include "TestSceneAssets.h"
#include "TestSceneAnimation.h"
#include "TestSceneLighting.h"
#include "TestSceneWorld.h"
#include "TestSceneCamera.h"
#include "TestSceneCharacterSheet.h"
#include "TestSceneCombat.h"
#include "TestSceneGrid.h"
#include "TestSceneInput.h"
#include "TestSceneImages.h"
#include "TestSceneDialogue.h"
#include "TestSceneShapes.h"
#include "TestSceneSpriteStress.h"
#include "TestSceneTileMap.h"
#include "TestSceneTokens.h"
#include "TestSceneTools.h"

#include <yorehold/framework/testing/TestBrowser.h>

#include <SDL3/SDL_main.h>

int main(int argc, char** argv)
{
    yh::TestBrowser browser;
    browser.add<TestSceneShapes>("Shapes");
    browser.add<TestSceneSpriteStress>("Sprite stress");
    browser.add<TestSceneInput>("Input");
    browser.add<TestSceneCamera>("M1 Camera");
    browser.add<TestSceneGrid>("M2 Grid");
    browser.add<TestSceneTileMap>("M3 Tile map");
    browser.add<TestSceneTokens>("M4 Tokens");
    browser.add<TestSceneCharacterSheet>("M5 Sheet");
    browser.add<TestSceneCombat>("M6 Combat");
    browser.add<TestSceneAssets>("F2 Skins + text");
    browser.add<TestSceneAnimation>("F4 Animation + particles");
    browser.add<TestSceneLighting>("F5 Lighting + fog");
    browser.add<TestSceneWorld>("F6 World + UI + audio");
    browser.add<TestSceneTools>("F7 Tools + text");
    browser.add<TestSceneImages>("F8 Images");
    browser.add<TestSceneDialogue>("F9 Dialogue");

    yh::HostSettings settings;
    settings.title = "yorehold-framework tests";
    settings.feedbackDir = YH_FEEDBACK_DIR;
    settings.stateDir = YH_DEV_STATE_DIR;
    return browser.run(argc, argv, settings);
}
