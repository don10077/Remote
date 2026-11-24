#include "remoteReadOutcome.h"

namespace harmony::app {

// Решение о формате здесь нет: оно живёт в ядре (`configReaders()` и вход `open()`), здесь
// соединяются ответ входа и галочка пользователя (правило 8).
ReadOutcome readOutcome(const Result<ConfigDocument>& opened, bool openInEditor) {
    return openInEditor && opened ? ReadOutcome::OpenInEditor : ReadOutcome::KeepFile;
}

} // namespace harmony::app
