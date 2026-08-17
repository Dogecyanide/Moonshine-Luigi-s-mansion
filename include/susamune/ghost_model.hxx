#pragma once

class TMarDirector;

namespace GhostModel {

void init();
void beginFrame();
void beforeStageSetup();
void onStageSetup(TMarDirector *director);
bool available();
bool submitted(bool secondary = false);

}  // namespace GhostModel
