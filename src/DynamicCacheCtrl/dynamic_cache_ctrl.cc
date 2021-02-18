#include "dynamic_cache_ctrl.hh"
#include "debug/DynamicCacheCtrl.hh"
#include "base/statistics.hh"

#define LOG(X) DPRINTF(DynamicCacheCtrl, "%s\n\n", X);

#define USING_CACHE 0
#define USING_NONE 1

DynamicCacheCtrl* dynamic_cache_global;

DynamicCacheCtrl::DynamicCacheCtrl(DynamicCacheCtrlParams* params) : 
    SimObject(params),
    cpu_side(params->name + ".cpu_side", this),
    mem_side(params->name + ".mem_side", this),
    cache_side_small(params->name + ".cache_side_small", this),
    cache_side_medium(params->name + ".cache_side_medium", this),
    cache_side_large(params->name + ".cache_side_large", this),
    cache_small(params->cache_small),
    cache_medium(params->cache_medium),
    cache_large(params->cache_large),
    cpu_object(params->cpu_object),
    blocked_packet(0),
    current_state(USING_NONE),
    lastStatDump(0),
    lastFlushReq(0),
    justDumped(false),
    cacheFlushWait(false),
    needCPURetry(false),
    accountFlush(params->accountFlush)
{
    dynamic_cache_global = this;    
    num_flushes = 0;
}

DynamicCacheCtrl*
DynamicCacheCtrlParams::create() 
{
    return new DynamicCacheCtrl(this);
}

void
DynamicCacheCtrl::notifyFlush()
{
    if(cacheFlushWait)
    {
       LOG("Cache Flush Completed");

       //TODO: a moving average w flush_ticks
       std::cout << "took " 
                 << curTick() - lastFlushReq
                 << " ticks for flush" 
                 << std::endl;

       //CPU is waiting for flush to end so tell it to resend
       cacheFlushWait = false;
       cpu_side.sendRetryReq();
    }
}

Port&
DynamicCacheCtrl::getPort(const std:: string& if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "No vector ports support");
    if (if_name == "cpu_side") 
        return cpu_side;
    else if (if_name == "mem_side") 
        return mem_side;
    else if (if_name == "cache_side_small") 
        return cache_side_small;
    else if (if_name == "cache_side_medium") 
        return cache_side_medium;
    else if (if_name == "cache_side_large") 
        return cache_side_large;
    else
        return SimObject::getPort(if_name, idx);
}

bool
DynamicCacheCtrl::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    return owner->handleTimingReq(pkt);
}

DynamicCacheCtrl::MemSidePort*
DynamicCacheCtrl::mem_port_to_use(bool& needCacheFlush)
{
    bool next_state;

    /* Setting next_state depending on stats */
    Counter current_inst = cpu_object -> numSimulatedInsts();

    //Right it starts with no cache -> cache -> no cache
    //if(current_inst < 2000000)
    if(1)
    {
        next_state = USING_NONE;
    }
    else
    {
        next_state = USING_CACHE;
    }

    //current_inst may never reach 1mil so mod won't work
    if(current_inst > lastStatDump + 1000000) 
    {
        LOG("Dumping Stats");
        //Stats::dump();
        lastStatDump += 1000000;

    }

    /* Handle Cache writebacks / invalidations */
    if(current_state == USING_CACHE && next_state == USING_NONE)
    {
        LOG("Switching from USING_CACHE to USING_NONE");
        std::cout << "CACHE -> NOCACHE" << std::endl;
        needCacheFlush = true;
        
    }

    else if(current_state == USING_NONE && next_state == USING_CACHE)
    {
        LOG("Switching from USING_NONE to USING_CACHE");
        std::cout << "NOCACHE -> CACHE" << std::endl;
    }

    current_state = next_state;


    /* return port object */
    if(next_state == USING_NONE)
        return &mem_side;
    else
        return &cache_side_small;


}

/* handleTimingReq handles sending packets */
bool
DynamicCacheCtrl::handleTimingReq(PacketPtr pkt)
{
    //CPU sent a packet but we will reject it
    //requires notification
    if(blocked_packet)
    {
        needCPURetry = true;
        return false;
    }

    //Right now the switch occurs based on the current Tick
    bool needCacheFlush = false;
    MemSidePort* port_to_use = mem_port_to_use(needCacheFlush);

    bool result = false; 


    if(needCacheFlush && accountFlush)
    {
        LOG("Cache Flush requested");
        lastFlushReq = curTick();
        cacheFlushWait = true;
        RequestPtr req = std::make_shared<Request>(0, 10, 0, 0);
        Packet* newpkt = new Packet(req, MemCmd::FlushReq);
        result = cache_side_small.sendTimingReq(newpkt);
        assert(result);
        return false;
    }

    else
    {
        result = (port_to_use) -> sendTimingReq(pkt);
    }


    // if mem_side is unable to send packet, store/retry
    if (!result) 
    {
        blocked_packet = pkt;    
    }

    //Returns true because CPU shouldn't worry about this blocked pkt
    return true;
}

void
DynamicCacheCtrl::MemSidePort::recvReqRetry() 
{
    assert(owner->blocked_packet);

    if (sendTimingReq(owner->blocked_packet)) 
    {
        owner->blocked_packet = 0;
    }

    // We might need CPU to retry its packet if we declined
    // when it first arrived
    if(owner->needCPURetry)
    {
        owner->cpu_side.sendRetryReq();
        owner->needCPURetry = false;
    }

}

bool
DynamicCacheCtrl::handleRecvTimingResp(PacketPtr pkt)
{
    return cpu_side.sendTimingResp(pkt);
}

bool
DynamicCacheCtrl::MemSidePort::recvTimingResp(PacketPtr pkt) 
{
    return owner->handleRecvTimingResp(pkt); 
}


void
DynamicCacheCtrl::regStats()
{
    SimObject::regStats();
    flush_ticks.name(name() + ".flushTicks").desc("Ticks taken to flush");
    num_flushes.name(name() + ".numFlushes").desc("Number of Flushes Taken");
}
