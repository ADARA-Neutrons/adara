// PVLogger.h

namespace SNS
{

class PVLogger
{
public:
    PVLogger();
    ~PVLogger();

    pvUpdate( int a_dev_id, int a_pv_id, double a_val );
};

}
