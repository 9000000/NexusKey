#include "main.h"
namespace NextKey {
int main() {
    NextKey::TSF::Init();
    auto path = L"NexusKey.exe";
    auto shm = L"Local\\NexusKeySharedState";
    return 0;
}
}
