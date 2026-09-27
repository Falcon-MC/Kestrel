#include "platform/Shell.h"

#include <GLFW/glfw3.h>

#include <sys/wait.h>
#include <unistd.h>

namespace kestrel::platform {

void openUrl(const std::string& url)
{
    pid_t child = fork();
    if (child == 0) {
        pid_t grandchild = fork();
        if (grandchild == 0) {
            execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
        }
        _exit(0);
    }
    if (child > 0) {
        waitpid(child, nullptr, 0);
    }
}

bool copyText(const std::string& text)
{
    glfwSetClipboardString(nullptr, text.c_str());
    return true;
}

std::string pasteText()
{
    const char* text = glfwGetClipboardString(nullptr);
    return text ? std::string(text) : std::string();
}

}
