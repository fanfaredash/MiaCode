#pragma once

#include <Qt>

namespace miacode::quick_shell {

constexpr bool isMenuToggleActivationKey(int key)
{
    return key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter;
}

}  // namespace miacode::quick_shell
