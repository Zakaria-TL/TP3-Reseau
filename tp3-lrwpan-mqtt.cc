/*
 * TP3 - IEEE 802.15.4 channel access study (NS-3.49)
 * 1 gateway (node 0) + N sensors (nodes 1..N), star topology.
 * Stack: LR-WPAN (802.15.4 CSMA/CA) + 6LoWPAN + IPv6 + UDP (port 1883).
 *
 * NOTE: NS-3 has no MQTT module. MQTT "publish" (QoS 0) is emulated by
 * periodic small UDP packets sent by the sensors (clients) to the gateway
 * (broker) on port 1883.
 *
 * Usage (copy to ns-3.49/scratch/):
 *   ./ns3 run "scratch/tp3-lrwpan-mqtt --nNodes=10 --interval=1 --txPower=0 --out=res.csv"
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/sixlowpan-module.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>

using namespace ns3;
using namespace ns3::lrwpan; // lr-wpan classes live in this namespace in ns-3.49

NS_LOG_COMPONENT_DEFINE("TP3LrWpanMqtt");

// ---- collision / drop counters (proxies, see report) ----
static uint32_t g_phyRxDrop = 0; // frames lost at PHY (collision / low SINR)
static uint32_t g_macTxDrop = 0; // frames dropped at MAC (CSMA-CA failure / retries exhausted)

static void
PhyRxDropCb(Ptr<const Packet> p)
{
    g_phyRxDrop++;
}

static void
MacTxDropCb(Ptr<const Packet> p)
{
    g_macTxDrop++;
}

int
main(int argc, char* argv[])
{
    uint32_t nNodes = 5;    // number of sensors
    double interval = 1.0;  // send interval (s)
    double txPower = 0.0;   // dBm
    double radius = 10.0;   // distance sensors <-> gateway (m)
    uint32_t pktSize = 40;  // MQTT-like payload (bytes)
    double simTime = 100.0; // s
    uint32_t run = 1;       // RNG run (seed)
    bool anim = false;
    bool xml = false;
    std::string out = "results.csv";

    CommandLine cmd(__FILE__);
    cmd.AddValue("nNodes", "Number of sensor nodes", nNodes);
    cmd.AddValue("interval", "Send interval in seconds", interval);
    cmd.AddValue("txPower", "Transmit power in dBm", txPower);
    cmd.AddValue("radius", "Sensor distance to gateway (m)", radius);
    cmd.AddValue("pktSize", "Payload size (bytes)", pktSize);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.AddValue("run", "RNG run number", run);
    cmd.AddValue("anim", "Generate NetAnim trace", anim);
    cmd.AddValue("xml", "Serialize FlowMonitor to XML", xml);
    cmd.AddValue("out", "CSV output file (appended)", out);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(run);

    // ---------------- Step 1: nodes + topology ----------------
    NodeContainer nodes;
    nodes.Create(nNodes + 1); // node 0 = gateway

    // Star topology: gateway at origin, sensors evenly spaced on a circle
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    pos->Add(Vector(0, 0, 0));
    for (uint32_t i = 0; i < nNodes; i++)
    {
        double a = 2 * M_PI * i / nNodes;
        pos->Add(Vector(radius * std::cos(a), radius * std::sin(a), 0));
    }
    MobilityHelper mob;
    mob.SetPositionAllocator(pos);
    mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mob.Install(nodes);

    // ---------------- Step 2: LR-WPAN + 6LoWPAN + IPv6 ----------------
    LrWpanHelper lrWpan;
    NetDeviceContainer lrDevs = lrWpan.Install(nodes);
    // Sets the PAN ID AND gives each device a unique 16-bit short address.
    // (Without it all nodes get 0xffff -> IPv6 address collision.)
    lrWpan.CreateAssociatedPan(lrDevs, 0); // ns-3.49 name (AssociateToPan was removed)

    // Transmit power (Step 3): set the TX PSD on every PHY (channel 11)
    LrWpanSpectrumValueHelper svh;
    Ptr<SpectrumValue> psd = svh.CreateTxPowerSpectralDensity(txPower, 11);
    for (uint32_t i = 0; i < lrDevs.GetN(); i++)
    {
        Ptr<LrWpanNetDevice> d = DynamicCast<LrWpanNetDevice>(lrDevs.Get(i));
        if (d != nullptr)
        {
            d->GetPhy()->SetTxPowerSpectralDensity(psd);
        }
    }

    InternetStackHelper internet;
    internet.SetIpv4StackInstall(false);
    internet.Install(nodes);

    SixLowPanHelper sixlowpan;
    NetDeviceContainer sixDevs = sixlowpan.Install(lrDevs);

    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer ifaces = ipv6.Assign(sixDevs);
    // Gateway global address = ifaces.GetAddress(0, 1) (index 0 = link-local)

    // ---------------- Traffic: MQTT-like (broker = gateway) ----------------
    const uint16_t port = 1883;
    UdpServerHelper broker(port);
    ApplicationContainer brokerApp = broker.Install(nodes.Get(0));
    brokerApp.Start(Seconds(0.0));
    brokerApp.Stop(Seconds(simTime));

    Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
    const double appStart = 5.0; // wait for neighbor discovery to settle
    for (uint32_t i = 1; i <= nNodes; i++)
    {
        UdpClientHelper client(ifaces.GetAddress(0, 1), port);
        client.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
        client.SetAttribute("Interval", TimeValue(Seconds(interval)));
        client.SetAttribute("PacketSize", UintegerValue(pktSize));
        ApplicationContainer app = client.Install(nodes.Get(i));
        // random offset so sensors do not all publish at the same instant
        app.Start(Seconds(appStart + jitter->GetValue(0.0, interval)));
        app.Stop(Seconds(simTime));
    }

    // ---------------- Collision / drop traces ----------------
    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/$ns3::lrwpan::LrWpanNetDevice/Phy/PhyRxDrop",
        MakeCallback(&PhyRxDropCb));
    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/$ns3::lrwpan::LrWpanNetDevice/Mac/MacTxDrop",
        MakeCallback(&MacTxDropCb));

    // ---------------- FlowMonitor ----------------
    FlowMonitorHelper fmh;
    Ptr<FlowMonitor> monitor = fmh.InstallAll();

    std::unique_ptr<AnimationInterface> netanim;
    if (anim)
    {
        netanim = std::make_unique<AnimationInterface>("tp3-lrwpan.xml");
        netanim->UpdateNodeDescription(nodes.Get(0), "Gateway");
        netanim->UpdateNodeColor(nodes.Get(0), 255, 0, 0);
    }

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ---------------- Step 4: metrics ----------------
    monitor->CheckForLostPackets();
    Ptr<Ipv6FlowClassifier> cls = DynamicCast<Ipv6FlowClassifier>(fmh.GetClassifier6());
    auto stats = monitor->GetFlowStats();

    uint64_t tx = 0;
    uint64_t rx = 0;
    uint64_t rxBytes = 0;
    double delaySum = 0;
    for (auto& kv : stats)
    {
        Ipv6FlowClassifier::FiveTuple t = cls->FindFlow(kv.first);
        if (t.destinationPort != port)
        {
            continue; // ignore ICMPv6 / ND control flows
        }
        tx += kv.second.txPackets;
        rx += kv.second.rxPackets;
        rxBytes += kv.second.rxBytes;
        delaySum += kv.second.delaySum.GetSeconds();
    }
    double pdr = tx ? 100.0 * rx / tx : 0.0;
    double thr = rxBytes / (simTime - appStart);        // bytes/s (IP level, all sensors)
    double delayMs = rx ? 1000.0 * delaySum / rx : 0.0; // average end-to-end delay

    std::cout << "N=" << nNodes << " interval=" << interval << " Ptx=" << txPower
              << "dBm | tx=" << tx << " rx=" << rx << " PDR=" << pdr << "%"
              << " thr=" << thr << " B/s delay=" << delayMs << " ms"
              << " phyRxDrop=" << g_phyRxDrop << " macTxDrop=" << g_macTxDrop << std::endl;

    // append to CSV
    bool newFile = !std::ifstream(out).good();
    std::ofstream f(out, std::ios::app);
    if (newFile)
    {
        f << "nNodes,interval,txPower,run,tx,rx,pdr,throughput,delay_ms,phyRxDrop,macTxDrop\n";
    }
    f << nNodes << "," << interval << "," << txPower << "," << run << "," << tx << "," << rx
      << "," << pdr << "," << thr << "," << delayMs << "," << g_phyRxDrop << "," << g_macTxDrop
      << "\n";
    f.close();

    if (xml)
    {
        monitor->SerializeToXmlFile("tp3-flowmon.xml", true, true);
    }

    Simulator::Destroy();
    return 0;
}
