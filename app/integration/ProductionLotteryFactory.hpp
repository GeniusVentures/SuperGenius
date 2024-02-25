/**
 * @file       ProductionLotteryFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "verification/production/impl/production_lottery_impl.hpp"
#include "integration/VRFProviderFactory.hpp"
#include "integration/HasherFactory.hpp"

class ProductionLotteryFactory
{
public:
    static std::shared_ptr<sgns::verification::ProductionLottery> create()
    {
        return std::make_shared<sgns::verification::ProductionLotteryImpl>( //
            VRFProviderFactory::create(),                                   //
            HasherFactory::create(),

        );
    }
}