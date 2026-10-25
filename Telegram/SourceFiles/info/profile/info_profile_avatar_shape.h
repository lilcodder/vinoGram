#pragma once

#include "base/object_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Info::Profile {

[[nodiscard]] object_ptr<Ui::BoxContent> AvatarShapeBox(
	not_null<Main::Session*> session);

} // namespace Info::Profile
