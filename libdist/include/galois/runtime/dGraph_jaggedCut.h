/** partitioned graph wrapper for jaggedCut -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Contains the 2d jagged vertex-cut functionality to be used in dGraph.
 *
 * @author Roshan Dathathri <roshan@cs.utexas.edu>
 */
#ifndef _GALOIS_DIST_HGRAPHJVC_H
#define _GALOIS_DIST_HGRAPHJVC_H

#include "galois/runtime/dGraph.h"

template<typename NodeTy, typename EdgeTy, bool columnBlocked = false, bool moreColumnHosts = false, uint32_t columnChunkSize = 256, bool BSPNode = false, bool BSPEdge = false>
class hGraph_jaggedCut : public hGraph<NodeTy, EdgeTy, BSPNode, BSPEdge> {
  constexpr static const char* const GRNAME = "dGraph_jaggedCut";
public:
  typedef hGraph<NodeTy, EdgeTy, BSPNode, BSPEdge> base_hGraph;

private:
  unsigned numRowHosts;
  unsigned numColumnHosts;

  uint32_t dummyOutgoingNodes; // nodes without outgoing edges that are stored with nodes having outgoing edges (to preserve original ordering locality)

  std::vector<std::vector<std::pair<uint64_t, uint64_t>>> jaggedColumnMap;

  // factorize numHosts such that difference between factors is minimized
  void factorize_hosts() {
    numColumnHosts = sqrt(base_hGraph::numHosts);
    while ((base_hGraph::numHosts % numColumnHosts) != 0) numColumnHosts--;
    numRowHosts = base_hGraph::numHosts/numColumnHosts;
    assert(numRowHosts>=numColumnHosts);
    if (moreColumnHosts) {
      std::swap(numRowHosts, numColumnHosts);
    }
    if (base_hGraph::id == 0) {
      galois::gPrint("Cartesian grid: ", numRowHosts, " x ", numColumnHosts, "\n");
    }
  }

  unsigned gridRowID() const {
    return (base_hGraph::id / numColumnHosts);
  }

  unsigned gridRowID(unsigned id) const {
    return (id / numColumnHosts);
  }

  unsigned gridColumnID() const {
    return (base_hGraph::id % numColumnHosts);
  }

  unsigned gridColumnID(unsigned id) const {
    return (id % numColumnHosts);
  }

  unsigned getBlockID(unsigned rowID, uint64_t gid) const {
    assert(gid < base_hGraph::numGlobalNodes);
    for (auto h = 0U; h < base_hGraph::numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = jaggedColumnMap[rowID][h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    assert(false);
    return base_hGraph::numHosts;
  }

  unsigned getColumnHostIDOfBlock(uint32_t blockID) const {
    if (columnBlocked) {
      return (blockID / numRowHosts); // blocked, contiguous
    } else {
      return (blockID % numColumnHosts); // round-robin, non-contiguous
    }
  }

  unsigned getColumnHostID(unsigned rowID, uint64_t gid) const {
    assert(gid < base_hGraph::numGlobalNodes);
    uint32_t blockID = getBlockID(rowID, gid);
    return getColumnHostIDOfBlock(blockID);
  }

  uint32_t getColumnIndex(unsigned rowID, uint64_t gid) const {
    assert(gid < base_hGraph::numGlobalNodes);
    auto blockID = getBlockID(rowID, gid);
    auto h = getColumnHostIDOfBlock(blockID);
    uint32_t columnIndex = 0;
    for (auto b = 0U; b <= blockID; ++b) {
      if (getColumnHostIDOfBlock(b) == h) {
        uint64_t start, end;
        std::tie(start, end) = jaggedColumnMap[rowID][b];
        if (gid < end) {
          columnIndex += gid - start;
          break; // redundant
        } else {
          columnIndex += end - start;
        }
      }
    }
    return columnIndex;
  }

  // called only for those hosts with which it shares nodes
  bool isNotCommunicationPartner(unsigned host, typename base_hGraph::SyncType syncType, WriteLocation writeLocation, ReadLocation readLocation) {
    if (syncType == base_hGraph::syncReduce) {
      switch(writeLocation) {
        case writeSource:
          return (gridRowID() != gridRowID(host));
        case writeDestination:
        case writeAny:
          // columns do not match processor grid
          return false; 
        default:
          assert(false);
      }
    } else { // syncBroadcast
      switch(readLocation) {
        case readSource:
          return (gridRowID() != gridRowID(host));
        case readDestination:
        case readAny:
          // columns do not match processor grid
          return false; 
        default:
          assert(false);
      }
    }
    return false;
  }

public:
  // GID = localToGlobalVector[LID]
  std::vector<uint64_t> localToGlobalVector; // TODO use LargeArray instead
  // LID = globalToLocalMap[GID]
  std::unordered_map<uint64_t, uint32_t> globalToLocalMap;

  uint32_t numNodes;
  uint64_t numEdges;

  // Return the ID to which gid belongs after patition.
  unsigned getHostID(uint64_t gid) const {
    assert(gid < base_hGraph::numGlobalNodes);
    for (auto h = 0U; h < base_hGraph::numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = base_hGraph::gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    assert(false);
    return base_hGraph::numHosts;
  }

  // Return if gid is Owned by local host.
  bool isOwned(uint64_t gid) const {
    uint64_t start, end;
    std::tie(start, end) = base_hGraph::gid2host[base_hGraph::id];
    return gid >= start && gid < end;
  }

  // Return if gid is present locally (owned or mirror).
  virtual bool isLocal(uint64_t gid) const {
    assert(gid < base_hGraph::numGlobalNodes);
    if (isOwned(gid)) return true;
    return (globalToLocalMap.find(gid) != globalToLocalMap.end());
  }

  virtual uint32_t G2L(uint64_t gid) const {
    assert(isLocal(gid));
    return globalToLocalMap.at(gid);
  }

  virtual uint64_t L2G(uint32_t lid) const {
    return localToGlobalVector[lid];
  }

  // requirement: for all X and Y,
  // On X, nothingToSend(Y) <=> On Y, nothingToRecv(X)
  // Note: templates may not be virtual, so passing types as arguments
  virtual bool nothingToSend(unsigned host, typename base_hGraph::SyncType syncType, WriteLocation writeLocation, ReadLocation readLocation) {
    auto &sharedNodes = (syncType == base_hGraph::syncReduce) ? base_hGraph::mirrorNodes : base_hGraph::masterNodes;
    if (sharedNodes[host].size() > 0) {
      return isNotCommunicationPartner(host, syncType, writeLocation, readLocation);
    }
    return true;
  }
  virtual bool nothingToRecv(unsigned host, typename base_hGraph::SyncType syncType, WriteLocation writeLocation, ReadLocation readLocation) {
    auto &sharedNodes = (syncType == base_hGraph::syncReduce) ? base_hGraph::masterNodes : base_hGraph::mirrorNodes;
    if (sharedNodes[host].size() > 0) {
      return isNotCommunicationPartner(host, syncType, writeLocation, readLocation);
    }
    return true;
  }

  /** 
   * Constructor for jagged Cut graph
   */
  hGraph_jaggedCut(const std::string& filename, 
              const std::string& partitionFolder, unsigned host, 
              unsigned _numHosts, std::vector<unsigned>& scalefactor, 
              bool transpose = false) : base_hGraph(host, _numHosts) {
    if (transpose) {
      GALOIS_DIE("Transpose not supported for jagged vertex-cuts");
    }

    galois::StatTimer Tgraph_construct("TIME_GRAPH_CONSTRUCT", GRNAME);
    Tgraph_construct.start();
    galois::StatTimer Tgraph_construct_comm(
      "TIME_GRAPH_CONSTRUCT_COMM", GRNAME);

    // only used to determine node splits among hosts; abandonded later
    // for the FileGraph which mmaps appropriate regions of memory
    galois::graphs::OfflineGraph g(filename);

    base_hGraph::numGlobalNodes = g.size();
    base_hGraph::numGlobalEdges = g.sizeEdges();
    factorize_hosts();

    base_hGraph::computeMasters(g, scalefactor, false);

    // at this point gid2Host has pairs for how to split nodes among
    // hosts; pair has begin and end
    uint64_t nodeBegin = base_hGraph::gid2host[base_hGraph::id].first;
    typename galois::graphs::OfflineGraph::edge_iterator edgeBegin = 
      g.edge_begin(nodeBegin);

    uint64_t nodeEnd = base_hGraph::gid2host[base_hGraph::id].second;
    typename galois::graphs::OfflineGraph::edge_iterator edgeEnd = 
      g.edge_begin(nodeEnd);
    
    // file graph that is mmapped for much faster reading; will use this
    // when possible from now on in the code
    galois::graphs::FileGraph fileGraph;

    fileGraph.partFromFile(filename,
      std::make_pair(boost::make_counting_iterator<uint64_t>(nodeBegin), 
                     boost::make_counting_iterator<uint64_t>(nodeEnd)),
      std::make_pair(edgeBegin, edgeEnd),
      true);

    determineJaggedColumnMapping(g, fileGraph); // first pass of the graph file

    std::vector<uint64_t> prefixSumOfEdges; // TODO use LargeArray
    loadStatistics(g, fileGraph, prefixSumOfEdges); // second pass of the graph file

    base_hGraph::printStatistics();

    assert(prefixSumOfEdges.size() == numNodes);

    base_hGraph::graph.allocateFrom(numNodes, numEdges);

    if (numNodes > 0) {
      //assert(numEdges > 0);

      base_hGraph::graph.constructNodes();

      auto& base_graph = base_hGraph::graph;
      galois::do_all(
        galois::iterate((uint32_t)0, numNodes),
        [&] (auto n) {
          base_graph.fixEndEdge(n, prefixSumOfEdges[n]);
        },
        galois::loopname("EdgeLoading"),
        galois::timeit(),
        galois::no_stats()
      );

    }

    if (base_hGraph::numOwned != 0) {
      base_hGraph::beginMaster = G2L(base_hGraph::gid2host[base_hGraph::id].first);
    } else {
      // no owned nodes, therefore empty masters
      base_hGraph::beginMaster = 0; 
    }

    loadEdges(base_hGraph::graph, g, fileGraph); // third pass of the graph file

    fill_mirrorNodes(base_hGraph::mirrorNodes);

    galois::StatTimer Tthread_ranges("TIME_THREAD_RANGES", GRNAME);

    Tthread_ranges.start();
    base_hGraph::determine_thread_ranges(numNodes, prefixSumOfEdges);
    Tthread_ranges.stop();

    base_hGraph::determine_thread_ranges_master();
    base_hGraph::determine_thread_ranges_with_edges();
    base_hGraph::initialize_specific_ranges();

    Tgraph_construct.stop();

    Tgraph_construct_comm.start();
    base_hGraph::setup_communication();
    Tgraph_construct_comm.stop();
  }

private:

  void determineJaggedColumnMapping(galois::graphs::OfflineGraph& g, 
                      galois::graphs::FileGraph& fileGraph) {
    auto activeThreads = galois::runtime::activeThreads;
    galois::setActiveThreads(numFileThreads); // only use limited threads for reading file

    galois::Timer timer;
    timer.start();
    fileGraph.reset_byte_counters();
    size_t numColumnChunks = (base_hGraph::numGlobalNodes + columnChunkSize - 1)/columnChunkSize;
    std::vector<uint64_t> prefixSumOfInEdges(numColumnChunks); // TODO use LargeArray
    galois::do_all(
      galois::iterate(base_hGraph::gid2host[base_hGraph::id].first,
                      base_hGraph::gid2host[base_hGraph::id].second),
      [&] (auto src) {
        auto ii = fileGraph.edge_begin(src);
        auto ee = fileGraph.edge_end(src);
        for (; ii < ee; ++ii) {
          auto dst = fileGraph.getEdgeDst(ii);
          ++prefixSumOfInEdges[dst/columnChunkSize]; // racy-writes are fine; imprecise
        }
      },
      galois::loopname("CalculateIndegree"),
      galois::timeit(),
      galois::no_stats()
    );

    timer.stop();
    galois::gPrint("[", base_hGraph::id, "] In-degree calculation time: ", timer.get_usec()/1000000.0f,
        " seconds to read ", fileGraph.num_bytes_read(), " bytes (",
        fileGraph.num_bytes_read()/(float)timer.get_usec(), " MBPS)\n");

    galois::setActiveThreads(activeThreads); // revert to prior active threads

    // TODO move this to a common helper function
    std::vector<uint64_t> prefixSumOfThreadBlocks(activeThreads, 0);
    galois::on_each([&](unsigned tid, unsigned nthreads) {
        assert(nthreads == activeThreads);
        auto range = galois::block_range((size_t)0, numColumnChunks,
          tid, nthreads);
        auto begin = range.first;
        auto end = range.second;
        // find prefix sum of each block
        for (auto i = begin + 1; i < end; ++i) {
          prefixSumOfInEdges[i] += prefixSumOfInEdges[i-1];
        }
        if (begin < end) {
          prefixSumOfThreadBlocks[tid] = prefixSumOfInEdges[end - 1];
        } else {
          prefixSumOfThreadBlocks[tid] = 0;
        }
    });
    for (unsigned int i = 1; i < activeThreads; ++i) {
      prefixSumOfThreadBlocks[i] += prefixSumOfThreadBlocks[i-1];
    }
    galois::on_each([&](unsigned tid, unsigned nthreads) {
        assert(nthreads == activeThreads);
        if (tid > 0) {
          auto range = galois::block_range((size_t)0, numColumnChunks,
            tid, nthreads);
          // update prefix sum from previous block
          for (auto i = range.first; i < range.second; ++i) {
            prefixSumOfInEdges[i] += prefixSumOfThreadBlocks[tid-1];
          }
        }
    });

    jaggedColumnMap.resize(numColumnHosts);
    for (unsigned i = 0; i < base_hGraph::numHosts; ++i) {
      // partition based on indegree-count only

      auto pair = galois::graphs::divideNodesBinarySearch(
        prefixSumOfInEdges.size(), prefixSumOfInEdges.back(),
        0, 1, i, base_hGraph::numHosts, prefixSumOfInEdges).first;

      // pair is iterators; i_pair is uints
      auto i_pair = std::make_pair(*(pair.first), *(pair.second));
      
      i_pair.first *= columnChunkSize;
      i_pair.second *= columnChunkSize; 
      if (i_pair.first > base_hGraph::numGlobalNodes) i_pair.first = base_hGraph::numGlobalNodes;
      if (i_pair.second > base_hGraph::numGlobalNodes) i_pair.second = base_hGraph::numGlobalNodes;
      jaggedColumnMap[gridColumnID()].push_back(i_pair);
    }

    auto& net = galois::runtime::getSystemNetworkInterface();
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      unsigned h = (gridRowID() * numColumnHosts) + i;
      if (h == base_hGraph::id) continue;
      galois::runtime::SendBuffer b;
      galois::runtime::gSerialize(b, jaggedColumnMap[gridColumnID()]);
      net.sendTagged(h, galois::runtime::evilPhase, b);
    }
    net.flush();

    for (unsigned i = 1; i < numColumnHosts; ++i) {
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      } while (!p);
      unsigned h = (p->first % numColumnHosts);
      auto& b = p->second;
      galois::runtime::gDeserialize(b, jaggedColumnMap[h]);
    }
    ++galois::runtime::evilPhase;
  }

  void loadStatistics(galois::graphs::OfflineGraph& g, 
                      galois::graphs::FileGraph& fileGraph, 
                      std::vector<uint64_t>& prefixSumOfEdges) {
    base_hGraph::numOwned = base_hGraph::gid2host[base_hGraph::id].second - base_hGraph::gid2host[base_hGraph::id].first;

    std::vector<galois::DynamicBitSet> hasIncomingEdge(numColumnHosts);
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      uint64_t columnBlockSize = 0;
      for (auto b = 0U; b < base_hGraph::numHosts; ++b) {
        if (getColumnHostIDOfBlock(b) == i) {
          uint64_t start, end;
          std::tie(start, end) = jaggedColumnMap[gridColumnID()][b];
          columnBlockSize += end - start;
        }
      }
      hasIncomingEdge[i].resize(columnBlockSize);
    }

    std::vector<std::vector<uint64_t> > numOutgoingEdges(numColumnHosts);
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      numOutgoingEdges[i].assign(base_hGraph::numOwned, 0);
    }

    galois::Timer timer;
    timer.start();
    fileGraph.reset_byte_counters();
    uint64_t rowOffset = base_hGraph::gid2host[base_hGraph::id].first;

    galois::do_all(
      galois::iterate(base_hGraph::gid2host[base_hGraph::id].first,
                      base_hGraph::gid2host[base_hGraph::id].second),
      [&] (auto src) {
        auto ii = fileGraph.edge_begin(src);
        auto ee = fileGraph.edge_end(src);
        for (; ii < ee; ++ii) {
          auto dst = fileGraph.getEdgeDst(ii);
          auto h = this->getColumnHostID(this->gridColumnID(), dst);
          hasIncomingEdge[h].set(this->getColumnIndex(this->gridColumnID(), dst));
          numOutgoingEdges[h][src - rowOffset]++;
        }
      },
      galois::loopname("EdgeInspection"),
      galois::timeit(),
      galois::no_stats()
    );

    timer.stop();
    galois::gPrint("[", base_hGraph::id, "] Edge inspection time: ", timer.get_usec()/1000000.0f, 
        " seconds to read ", fileGraph.num_bytes_read(), " bytes (",
        fileGraph.num_bytes_read()/(float)timer.get_usec(), " MBPS)\n");

    auto& net = galois::runtime::getSystemNetworkInterface();
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      unsigned h = (gridRowID() * numColumnHosts) + i;
      if (h == base_hGraph::id) continue;
      galois::runtime::SendBuffer b;
      galois::runtime::gSerialize(b, numOutgoingEdges[i]);
      galois::runtime::gSerialize(b, hasIncomingEdge[i]);
      net.sendTagged(h, galois::runtime::evilPhase, b);
    }
    net.flush();

    for (unsigned i = 1; i < numColumnHosts; ++i) {
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      } while (!p);
      unsigned h = (p->first % numColumnHosts);
      auto& b = p->second;
      galois::runtime::gDeserialize(b, numOutgoingEdges[h]);
      galois::runtime::gDeserialize(b, hasIncomingEdge[h]);
    }
    ++galois::runtime::evilPhase;

    auto max_nodes = hasIncomingEdge[0].size(); // imprecise
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      max_nodes += numOutgoingEdges[i].size();
    }
    localToGlobalVector.reserve(max_nodes);
    globalToLocalMap.reserve(max_nodes);
    prefixSumOfEdges.reserve(max_nodes);
    unsigned leaderHostID = gridRowID() * numColumnHosts;
    uint64_t src = base_hGraph::gid2host[leaderHostID].first;
    uint64_t src_end = base_hGraph::gid2host[leaderHostID+numColumnHosts-1].second;
    dummyOutgoingNodes = 0;
    numNodes = 0;
    numEdges = 0;
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      for (uint32_t j = 0; j < numOutgoingEdges[i].size(); ++j) {
        bool createNode = false;
        if (numOutgoingEdges[i][j] > 0) {
          createNode = true;
          numEdges += numOutgoingEdges[i][j];
        } else if (isOwned(src)) {
          createNode = true;
        } else {
          for (unsigned k = 0; k < numColumnHosts; ++k) {
            auto h = getColumnHostID(k, src);
            if (h == gridColumnID()) {
              if (hasIncomingEdge[k].test(getColumnIndex(k, src))) {
                createNode = true;
                ++dummyOutgoingNodes;
                break;
              }
            }
          }
        }
        if (createNode) {
          localToGlobalVector.push_back(src);
          globalToLocalMap[src] = numNodes++;
          prefixSumOfEdges.push_back(numEdges);
        }
        ++src;
      }
    }
    assert(src == src_end);

    base_hGraph::numNodesWithEdges = numNodes;

    src = base_hGraph::gid2host[leaderHostID].first;
    for (uint64_t dst = 0; dst < base_hGraph::numGlobalNodes; ++dst) {
      if (src != src_end) {
        if (dst == src) { // skip nodes which have been allocated above
          dst = src_end - 1;
          continue;
        }
      }
      assert((dst < src) || (dst >= src_end));
      bool createNode = false;
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        auto h = getColumnHostID(i, dst);
        if (h == gridColumnID()) {
          if (hasIncomingEdge[i].test(getColumnIndex(i, dst))) {
            createNode = true;
            break;
          }
        }
      }
      if (createNode) {
        localToGlobalVector.push_back(dst);
        globalToLocalMap[dst] = numNodes++;
        prefixSumOfEdges.push_back(numEdges);
      }
    }
  }

  template<typename GraphTy>
  void loadEdges(GraphTy& graph, 
                 galois::graphs::OfflineGraph& g,
                 galois::graphs::FileGraph& fileGraph) {
    if (base_hGraph::id == 0) {
      if (std::is_void<typename GraphTy::edge_data_type>::value) {
        galois::gPrint("Loading void edge-data while creating edges\n");
      } else {
        galois::gPrint("Loading edge-data while creating edges\n");
      }
    }

    galois::Timer timer;
    timer.start();
    fileGraph.reset_byte_counters();

    uint32_t numNodesWithEdges;
    numNodesWithEdges = base_hGraph::numOwned + dummyOutgoingNodes;
    // TODO: try to parallelize this better
    galois::on_each([&](unsigned tid, unsigned nthreads){
      if (tid == 0) loadEdgesFromFile(graph, g, fileGraph);
      // using multiple threads to receive is mostly slower and leads to a deadlock or hangs sometimes
      if ((nthreads == 1) || (tid == 1)) receiveEdges(graph, numNodesWithEdges);
    });
    ++galois::runtime::evilPhase;

    timer.stop();
    galois::gPrint("[", base_hGraph::id, "] Edge loading time: ", timer.get_usec()/1000000.0f, 
        " seconds to read ", fileGraph.num_bytes_read(), " bytes (",
        fileGraph.num_bytes_read()/(float)timer.get_usec(), " MBPS)\n");
  }

  template<typename GraphTy, typename std::enable_if<!std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void loadEdgesFromFile(GraphTy& graph, 
                         galois::graphs::OfflineGraph& g,
                         galois::graphs::FileGraph& fileGraph) {
    unsigned h_offset = gridRowID() * numColumnHosts;
    auto& net = galois::runtime::getSystemNetworkInterface();
    std::vector<std::vector<uint64_t>> gdst_vec(numColumnHosts);
    std::vector<std::vector<typename GraphTy::edge_data_type>> gdata_vec(numColumnHosts);

    auto ee = fileGraph.edge_begin(base_hGraph::gid2host[base_hGraph::id].first);
    for (auto n = base_hGraph::gid2host[base_hGraph::id].first; n < base_hGraph::gid2host[base_hGraph::id].second; ++n) {
      uint32_t lsrc = 0;
      uint64_t cur = 0;
      if (isLocal(n)) {
        lsrc = G2L(n);
        cur = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
      }
      auto ii = ee;
      ee = fileGraph.edge_end(n);
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        gdst_vec[i].clear();
        gdata_vec[i].clear();
        gdst_vec[i].reserve(std::distance(ii, ee));
        gdata_vec[i].reserve(std::distance(ii, ee));
      }
      for (; ii < ee; ++ii) {
        uint64_t gdst = fileGraph.getEdgeDst(ii);
        auto gdata = fileGraph.getEdgeData<typename GraphTy::edge_data_type>(ii);
        int i = getColumnHostID(gridColumnID(), gdst);
        if ((h_offset + i) == base_hGraph::id) {
          assert(isLocal(n));
          uint32_t ldst = G2L(gdst);
          graph.constructEdge(cur++, ldst, gdata);
        } else {
          gdst_vec[i].push_back(gdst);
          gdata_vec[i].push_back(gdata);
        }
      }
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        if (gdst_vec[i].size() > 0) {
          galois::runtime::SendBuffer b;
          galois::runtime::gSerialize(b, n);
          galois::runtime::gSerialize(b, gdst_vec[i]);
          galois::runtime::gSerialize(b, gdata_vec[i]);
          net.sendTagged(h_offset + i, galois::runtime::evilPhase, b);
        }
      }
      if (isLocal(n)) {
        assert(cur == (*graph.edge_end(lsrc)));
      }
    }
    net.flush();
  }

  template<typename GraphTy, typename std::enable_if<std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void loadEdgesFromFile(GraphTy& graph, 
                         galois::graphs::OfflineGraph& g,
                         galois::graphs::FileGraph& fileGraph) {
    unsigned h_offset = gridRowID() * numColumnHosts;
    auto& net = galois::runtime::getSystemNetworkInterface();
    std::vector<std::vector<uint64_t>> gdst_vec(numColumnHosts);

    auto ee = fileGraph.edge_begin(base_hGraph::gid2host[base_hGraph::id].first);
    for (auto n = base_hGraph::gid2host[base_hGraph::id].first; n < base_hGraph::gid2host[base_hGraph::id].second; ++n) {
      uint32_t lsrc = 0;
      uint64_t cur = 0;
      if (isLocal(n)) {
        lsrc = G2L(n);
        cur = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
      }
      auto ii = ee;
      ee = fileGraph.edge_end(n);
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        gdst_vec[i].clear();
        gdst_vec[i].reserve(std::distance(ii, ee));
      }
      for (; ii < ee; ++ii) {
        uint64_t gdst = fileGraph.getEdgeDst(ii);
        int i = getColumnHostID(gridColumnID(), gdst);
        if ((h_offset + i) == base_hGraph::id) {
          assert(isLocal(n));
          uint32_t ldst = G2L(gdst);
          graph.constructEdge(cur++, ldst);
        } else {
          gdst_vec[i].push_back(gdst);
        }
      }
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        if (gdst_vec[i].size() > 0) {
          galois::runtime::SendBuffer b;
          galois::runtime::gSerialize(b, n);
          galois::runtime::gSerialize(b, gdst_vec[i]);
          net.sendTagged(h_offset + i, galois::runtime::evilPhase, b);
        }
      }
      if (isLocal(n)) {
        assert(cur == (*graph.edge_end(lsrc)));
      }
    }
    net.flush();
  }

  template<typename GraphTy>
  void receiveEdges(GraphTy& graph, uint32_t& numNodesWithEdges) {
    auto& net = galois::runtime::getSystemNetworkInterface();
    while (numNodesWithEdges < base_hGraph::numNodesWithEdges) {
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      net.handleReceives();
      p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      if (p) {
        auto& rb = p->second;
        uint64_t n;
        galois::runtime::gDeserialize(rb, n);
        std::vector<uint64_t> gdst_vec;
        galois::runtime::gDeserialize(rb, gdst_vec);
        assert(isLocal(n));
        uint32_t lsrc = G2L(n);
        uint64_t cur = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
        uint64_t cur_end = *graph.edge_end(lsrc);
        assert((cur_end - cur) == gdst_vec.size());
        deserializeEdges(graph, rb, gdst_vec, cur, cur_end);
        ++numNodesWithEdges;
      }
    }
  }

  template<typename GraphTy, typename std::enable_if<!std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b, 
      std::vector<uint64_t>& gdst_vec, uint64_t& cur, uint64_t& cur_end) {
    std::vector<typename GraphTy::edge_data_type> gdata_vec;
    galois::runtime::gDeserialize(b, gdata_vec);
    uint64_t i = 0;
    while (cur < cur_end) {
      auto gdata = gdata_vec[i];
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst, gdata);
    }
  }

  template<typename GraphTy, typename std::enable_if<std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b, 
      std::vector<uint64_t>& gdst_vec, uint64_t& cur, uint64_t& cur_end) {
    uint64_t i = 0;
    while (cur < cur_end) {
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst);
    }
  }

  void fill_mirrorNodes(std::vector<std::vector<size_t>>& mirrorNodes){
    for (uint32_t i = 0; i < numNodes; ++i) {
      uint64_t gid = localToGlobalVector[i];
      unsigned hostID = getHostID(gid);
      if (hostID == base_hGraph::id) continue;
      mirrorNodes[hostID].push_back(gid);
    }
  }

public:

  std::string getPartitionFileName(const std::string& filename, const std::string & basename, unsigned hostID, unsigned num_hosts){
    return filename;
  }

  bool is_vertex_cut() const{
    if (moreColumnHosts) {
      // IEC and OEC will be reversed, so do not handle it as an edge-cut
      if ((numRowHosts == 1) && (numColumnHosts == 1)) return false;
    } else {
      if ((numRowHosts == 1) || (numColumnHosts == 1)) return false; // IEC or OEC
    }
    return true;
  }

  void reset_bitset(typename base_hGraph::SyncType syncType, 
                    void (*bitset_reset_range)(size_t, size_t)) const {
    if (base_hGraph::numOwned != 0) {
      auto endMaster = base_hGraph::beginMaster + base_hGraph::numOwned;
      if (syncType == base_hGraph::syncBroadcast) { // reset masters
        bitset_reset_range(base_hGraph::beginMaster, endMaster-1);
      } else { // reset mirrors
        assert(syncType == base_hGraph::syncReduce);
        if (base_hGraph::beginMaster > 0) {
          bitset_reset_range(0, base_hGraph::beginMaster - 1);
        }
        if (endMaster < numNodes) {
          bitset_reset_range(endMaster, numNodes - 1);
        }
      }
    }
  }
};
#endif
