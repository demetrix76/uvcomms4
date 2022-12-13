#pragma once

#include <concepts>
#include <utility>
#include <iostream>

namespace uvcomms4
{
    template<typename function_t>
    class final_act
    {
    public:
        template<typename fun_t>
            requires std::constructible_from<function_t, fun_t &&>
        final_act(fun_t && aFun) :
            mFunction(std::forward<fun_t>(aFun))
        {}

        void cancel() noexcept
        {
            mActive = false;
        }

        ~final_act() noexcept
        {
            try
            {
                if(mActive)
                    mFunction();
            }
            catch(...)
            {
                std::cerr << "WARNING: exception thrown by the final_act function\n";
            }
        }

    private:
        function_t  mFunction;
        bool        mActive { true };
    };

    template<typename fun_t>
    final_act(fun_t &&) -> final_act<std::decay_t<fun_t>>;

}
