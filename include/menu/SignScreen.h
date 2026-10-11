#pragma once

#include "client/SignEditor.h"
#include "ui/JsonUi.h"

namespace kestrel::menu {

class SignScreen {
public:
    bool active() const { return request.has_value(); }
    void setDefinitions(std::shared_ptr<const ui::JsonUi> value);
    void open(SignEditorRequest value);
    void reset();
    void draw(ui::Context& ui, float width, float height);
    std::optional<SignEditorAnswer> takeAnswer();

private:
    std::shared_ptr<const ui::JsonUi> definitions;
    std::unique_ptr<ui::JsonUiScreen> screen;
    std::optional<SignEditorRequest> request;
    std::optional<SignEditorAnswer> answer;
};

}
