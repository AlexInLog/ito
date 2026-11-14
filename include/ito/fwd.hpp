#pragma once

namespace ito
{
    class loop;

    template<typename T = void>
    class coro;

    template<typename T>
    class task;

} // namespace ito


namespace ito::async
{
    template<typename T = void>
    class promise;

    template<typename T = void>
    class future;

} // namespace ito::async
