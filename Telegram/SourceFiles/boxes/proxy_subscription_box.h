#pragma once

#include "base/object_ptr.h"

namespace Ui {
class BoxContent;
} // namespace Ui

[[nodiscard]] object_ptr<Ui::BoxContent> ProxySubscriptionBox();
