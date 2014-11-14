#ifndef MIDDLEBOX_PLACEMENT_SRC_CPLEX_H
#define MIDDLEBOX_PLACEMENT_SRC_CPLEX_H

#include "datastructure.h"
#include "util.h"
#include <string>
#include <cmath>
#include <climits>

#include <ilcplex/ilocplex.h>
ILOSTLBEGIN

typedef IloArray<IloIntVarArray> IloIntVar2dArray;
typedef IloArray<IloIntVar2dArray> IloIntVar3dArray;
typedef IloArray<IloIntVar3dArray> IloIntVar4dArray;
typedef IloArray<IloIntVar4dArray> IloIntVar5dArray;

typedef IloArray<IloIntArray> IloInt2dArray;
typedef IloArray<IloInt2dArray> IloInt3dArray;

typedef IloArray<IloExprArray> IloExpr2dArray;

//#define INF 9999999

void print_IloInt2dArray(IloInt2dArray a, int dimension1, int dimension2,
                         string name) {
  cout << name << endl;
  for (int i = 0; i < dimension1; ++i) {
    for (int j = 0; j < dimension2; ++j) {
      cout << a[i][j] << " ";
    }
    cout << endl;
  }
}

void print_IloInt3dArray(IloInt3dArray a, int dimension1, int dimension2,
                         int dimension3, string name) {
  cout << name << endl;
  for (int i = 0; i < dimension1; ++i) {
    cout << "dim1 = " << i << endl;
    for (int j = 0; j < dimension2; ++j) {
      for (int k = 0; k < dimension3; ++k) {
        cout << a[i][j][k] << " ";
      }
      cout << endl;
    }
    cout << endl;
  }
}

void run_cplex(std::vector<traffic_request> traffic_requests) {

  IloEnv env;
  try {
    // declare the model and the solver
    IloModel model(env);
    IloCplex cplex(model);

    // save all the constraints for model checking
    IloConstraintArray cnst(env);
    IloNumArray pref(env);

    ///////////////////////////////////////////////////
    //  Physical Network                             //
    ///////////////////////////////////////////////////

    cout << "Modeling Physical Network..." << endl;

    int kSwitchCount = 0, kLinkCount = 0, kServerCount = 0, kResourceCount = 0;

    // read topology file and populate associated variables
    FILE *topology_file = fopen("topology", "r");

    // read switch and link count
    fscanf(topology_file, "%d %d", &kSwitchCount, &kLinkCount);
    // ingress and egress middleboxes are deployed on special non-existent
    // servers
    kServerCount += kSwitchCount; // these are the special ones
    kServerCount += kSwitchCount; // TODO: read from file, current assumption:
                                  // there is one server per switch
    kResourceCount = 1; // TODO: Read from file

    // Declare the required DSs
    std::vector<int> _nbr[kSwitchCount];
    int switch4server[kServerCount];
    std::vector<int> server4switch[kSwitchCount];
    int _beta[kSwitchCount][kSwitchCount];
    int _delta[kSwitchCount][kSwitchCount];
    int max_beta = 0, max_delta = 0;

    // read the switches, servers and resource capacity
    // TODO: need to change the topology file to include information about
    // servers

    //////////CPLEX Variable//////////
    //_z_s_n = 1, iff _n is attached to _s
    IloInt2dArray _z_s_n(env, kSwitchCount);
    for (int _s = 0; _s < kSwitchCount; ++_s) {
      _z_s_n[_s] = IloIntArray(env, kServerCount);
      for (int _n = 0; _n < kServerCount; ++_n) {
        _z_s_n[_s][_n] = 0;
      }
    }

    //////////CPLEX Variable//////////
    // c_nr[n][r] = R+, denote the resource capacity of server _n for resource r
    // TODO: now we just have a single resource (CPU), we should include others
    IloInt2dArray c_nr(env, kServerCount);
    for (int _n = 0; _n < kServerCount; ++_n) {
      c_nr[_n] = IloIntArray(env, 2, 0, 1); // TODO: should be kResourceCount
      for (int r = 0; r < kResourceCount; r++) {
        c_nr[_n][r] = 0;
      }
    }

    // read the switch info from file
    for (int i = 0, sw, cpu; i < kSwitchCount; ++i) {
      fscanf(topology_file, "%d %d", &sw, &cpu);
      _z_s_n[sw][sw] = 1; // for the special ones
      _z_s_n[sw][sw + kSwitchCount] =
          1; // these is where we deploy normal middleboxes

      switch4server[i] = i;
      switch4server[i + kSwitchCount] = i;
      server4switch[i].push_back(i);
      server4switch[i].push_back(i + kSwitchCount);

      c_nr[sw][0] = 0;
      c_nr[sw + kSwitchCount][0] = cpu;
      // TODO: reflect changes if server and resource info changed
    }
    // print_IloInt2dArray(_z_s_n, kSwitchCount, kServerCount, "_z_s_n");
    // print_IloInt2dArray(c_nr, kServerCount, kResourceCount, "c_nr");

    // initialize _beta & _delta to remove any garbage value
    for (int _u = 0; _u < kSwitchCount; ++_u) {
      for (int _v = 0; _v < kSwitchCount; ++_v) {
        _beta[_u][_v] = _delta[_u][_v] = 0;
      }
    }

    // read link info from file
    for (int _l = 0, _u, _v, b, d; _l < kLinkCount; ++_l) {
      fscanf(topology_file, "%d %d %d %d", &_u, &_v, &b, &d);
      _beta[_u][_v] = b;
      _beta[_v][_u] = b;
      _delta[_u][_v] = d;
      _delta[_v][_u] = d;
      //_nbr
      _nbr[_u].push_back(_v);
      _nbr[_v].push_back(_u);
      // max_beta & max_delta
      max_beta = max(max_beta, b);
      max_delta = max(max_delta, d);
    }

    // print_IloInt2dArray(_u_s_l, kSwitchCount, kLinkCount, "_u_s_l");
    // print_IloInt2dArray(_v_s_l, kSwitchCount, kLinkCount, "_v_s_l");

    fclose(topology_file);

    cout << "Done." << endl;

    ///////////////////////////////////////////////////
    //  Middlebox                                    //
    ///////////////////////////////////////////////////

    cout << "Modeling the VNFs..." << endl;

    // we include 2 special type of middleboxes, ingree & egress
    // ingress is type 0
    // egress is type 1
    int kMboxTypes = middleboxes.size() + 2;

    //////////CPLEX Variable//////////
    // dp_n = 1, if middlebox of type p can be deployed on server _n
    IloInt2dArray dp_n(env, kMboxTypes);
    for (int p = 0; p < kMboxTypes; ++p) {
      dp_n[p] = IloIntArray(env, kServerCount, 0, 1);
    }
    // initialize dp_n
    for (int p = 0; p < kMboxTypes; ++p) {
      for (int _n = 0; _n < kServerCount; ++_n) {
        dp_n[p][_n] = 0;
      }
    }
    // middlebox type 0 & 1 can be deployed only on server
    // [0...(kSwitchCount-1)]
    for (int p = 0; p < 2; ++p) {
      for (int _n = 0; _n < kSwitchCount; ++_n) {
        dp_n[p][_n] = 1;
      }
    }
    // for now we assume that normal middleboxes can be deployed on any server
    // TODO: add data for middlebox placement constraint
    for (int p = 2; p < kMboxTypes; ++p) {
      for (int _n = kSwitchCount; _n < kServerCount; ++_n) {
        dp_n[p][_n] = 1;
      }
    }
    // print_IloInt2dArray(dp_n, kMboxTypes, kServerCount, "dp_d");

    // compute the number of total middleboxes
    int kMboxCount = 0;
    std::vector<int> server4mbox;
    std::vector<int> mboxType;
    std::vector<int> mbox4server[kServerCount];
    for (int p = 0, mcount; p < kMboxTypes; ++p) {
      for (int _n = 0; _n < kServerCount; ++_n) {
        if (dp_n[p][_n] == 1) {
          if (p == 0 || p == 1) {
            mbox4server[_n].push_back(kMboxCount);
            kMboxCount++;
            server4mbox.push_back(_n);
            mboxType.push_back(p);
          } else {
            //# of deployable mbox = resource-cap/resource-req
            mcount =
                floor(c_nr[_n][0] * 1.0 / middleboxes[p - 2].cpu_requirement);
            for (int i = 0; i < mcount; ++i) {
              server4mbox.push_back(_n);
              mboxType.push_back(p);
              mbox4server[_n].push_back(kMboxCount + i);
            }
            kMboxCount += mcount;
          }
        }
      }
    }

    //*****Augment the Physical Network************************************

    int switch4mbox[kMboxCount];

    int seed = kSwitchCount; // this is the starting number for the new switches
    // Number of switches in the augmented graph will be increased by kMboxCount
    kSwitchCount += kMboxCount;

    // Need to create a new _nbr DS
    std::vector<int> __nbr[kSwitchCount];
    // copy the old data
    for (int _u = 0; _u < seed; ++_u) {
      for (int _v : _nbr[_u]) {
        __nbr[_u].push_back(_v);
      }
    }
    int pseudo2actual[kSwitchCount]; // map the pseudo switch to the actuals

    // declare CPLEX varibales beta_u_v and delta_u_v, ### do not move,
    // kSwitchCount must be @ new value
    IloInt2dArray beta_u_v(env, kSwitchCount);
    IloInt2dArray delta_u_v(env, kSwitchCount);
    for (int _u = 0; _u < kSwitchCount; ++_u) {
      beta_u_v[_u] = IloIntArray(env, kSwitchCount, 0, max_beta);
      delta_u_v[_u] = IloIntArray(env, kSwitchCount, 0, max_delta);
      for (int _v = 0; _v < kSwitchCount; ++_v) {
        beta_u_v[_u][_v] = 0;
        delta_u_v[_u][_v] = 0;
      }
    }
    // copy old data from _beta and _delta
    for (int _u = 0; _u < seed; ++_u) {
      for (int _v = 0; _v < seed; ++_v) {
        beta_u_v[_u][_v] = _beta[_u][_v];
        delta_u_v[_u][_v] = _delta[_u][_v];
      }
    }

    std::vector<int> mbox4switch[kSwitchCount]; // do not move, kSwitchCount
                                                // must be @ new value
    for (int _n = 0; _n < kServerCount; ++_n) {
      for (int m : mbox4server[_n]) {

        int _u = switch4server[_n];
        int _v = seed++; // label for pseudo switch

        beta_u_v[_u][_v] = INF;
        beta_u_v[_v][_u] = INF;
        delta_u_v[_u][_v] = 0;
        delta_u_v[_v][_u] = 0;

        pseudo2actual[_v] = _u;

        __nbr[_u].push_back(_v);
        __nbr[_v].push_back(_u);

        switch4mbox[m] = _v;
        mbox4switch[_v].push_back(m);
      }
    }

    /*
    cout << "kMboxCount " << kMboxCount << endl;
    cout << "server4mbox" << endl;
    for (int i = 0; i < kMboxCount; ++i) {
      cout << server4mbox[i] << " ";
    }
    cout << endl;
    cout << "mboxType" << endl;
    for (int i = 0; i < kMboxCount; ++i) {
      cout << mboxType[i] << " ";
    }
    */
    /*
    cout << endl;
    cout << "mbox4server" <<endl;
    for (int _n = 0; _n < kServerCount; ++_n) {
      cout << "server " << _n << " ";
      for (int i = 0; i < mbox4server[_n].size(); ++i) {
        cout << mbox4server[_n][i] << " ";
      }
      cout << endl;
    }

    cout << endl;
    cout << "mbox4switch" <<endl;
    for (int _s = 0; _s < kSwitchCount; ++_s) {
      cout << "switch " << _s << " ";
      for (int i = 0; i < mbox4switch[_s].size(); ++i) {
        cout << mbox4switch[_s][i] << " ";
      }
      cout << endl;
    }
    */
    //*********************************************************************

    //////////CPLEX Variable//////////
    // D_m = R+, is the deployment cost of m
    // K_m = R+, is the processing capacity of m
    IloIntArray D_m(env, kMboxCount);
    IloIntArray K_m(env, kMboxCount);
    // initialize D_m
    for (int m = 0; m < kMboxCount; ++m) {
      if (mboxType[m] == 0 || mboxType[m] == 1) {
        D_m[m] = 0;
        K_m[m] = INF;
      } else {
        D_m[m] = middleboxes[mboxType[m] - 2].deployment_cost;
        K_m[m] = middleboxes[mboxType[m] - 2].processing_capacity;
      }
    }

    //////////CPLEX Variable//////////
    // amp = 1, if m is a middlebox of type p
    // this is same as mboxType, but we need this for the later parts
    // we cannot uses amp in the previous steps as the total count of
    // middleboxes is not known
    IloInt2dArray amp(env, kMboxCount);
    for (int m = 0; m < kMboxCount; ++m) {
      amp[m] = IloIntArray(env, kMboxTypes, 0, 1);
    }
    // Initialize
    for (int m = 0; m < kMboxCount; ++m) {
      for (int p = 0; p < kMboxTypes; ++p) {
        amp[m][p] = (mboxType[m] == p);
      }
    }
    // print_IloInt2dArray(amp, kMboxCount, kMboxTypes, "amp");

    //////////CPLEX Variable//////////
    // cmr = R+, is the r resource requirement for m
    IloInt2dArray cmr(env, kMboxCount);
    for (int m = 0; m < kMboxCount; ++m) {
      cmr[m] = IloIntArray(env, 2, 0, LONG_MAX);
    }
    // initialize
    for (int m = 0; m < kMboxCount; ++m) {
      if (mboxType[m] == 0 || mboxType[m] == 1) {
        cmr[m][0] = 0;
      } else {
        cmr[m][0] = middleboxes[mboxType[m] - 2].cpu_requirement;
      }
    }
    // TODO: add data for additional resources
    // print_IloInt2dArray(cmr, kMboxCount, 1, "cmr");

    //////////CPLEX Variable//////////
    // ym = 1; if middlebox m is active
    IloIntVarArray ym(env, kMboxCount, 0, 1);

    //-----CPLEX Constraint------------------------------------------------
    // ADD: physical server capacity constraint
    for (int _n = 0; _n < kServerCount; ++_n) {
      for (int r = 0; r < kResourceCount; ++r) {
        IloExpr sum(env);
        for (int i = 0, m; i < mbox4server[_n].size(); ++i) {
          m = mbox4server[_n][i];
          sum += ym[m] * cmr[m][r];
        }
        model.add(sum <= c_nr[_n][r]);
      }
    }
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: middlebox placement constraint
    for (int m = 0; m < kMboxCount; ++m) {
      for (int p = 0; p < kMboxTypes; ++p) {
        model.add(ym[m] * amp[m][p] <= dp_n[p][server4mbox[m]]);
      }
    }
    //---------------------------------------------------------------------

    /*
    //ADD: old middlebox constraint
    for (int _n = 0; _n < kServerCount; ++_n) {
      for (int m = 0; m < kMboxCount; ++m) {
        model.add(bm_n[m][_n] >= hat_bm_n[m][_n]);
        cnst.add(bm_n[m][_n] >= hat_bm_n[m][_n]);
      }
    }
    */

    cout << "Done." << endl;

    ///////////////////////////////////////////////////
    //  Traffic                                      //
    ///////////////////////////////////////////////////

    cout << "Modeling Traffic..." << endl;

    int kTrafficCount = traffic_requests.size();
    int trafficNodeCount[kTrafficCount], trafficLinkCount[kTrafficCount];
    std::vector<std::vector<int> > trafficNodeType;
    std::vector<std::vector<int> > nbr[kTrafficCount];
    for (int t = 0; t < kTrafficCount; ++t) {
      trafficNodeCount[t] = traffic_requests[t].middlebox_sequence.size() + 2;
      trafficLinkCount[t] = trafficNodeCount[t] - 1;
      std::vector<int> nodeType;
      nodeType.push_back(0);
      for (int i = 0; i < traffic_requests[t].middlebox_sequence.size(); ++i) {
        nodeType.push_back(traffic_requests[t].middlebox_sequence[i] + 2);
      }
      nodeType.push_back(1);
      trafficNodeType.push_back(nodeType);

      // nbr
      std::vector<int> firstNodeNbr;
      firstNodeNbr.push_back(1);
      nbr[t].push_back(firstNodeNbr);

      for (int i = 0; i < traffic_requests[t].middlebox_sequence.size(); ++i) {
        std::vector<int> nodeNbr;
        nodeNbr.push_back(i);
        nodeNbr.push_back(i + 2);
        nbr[t].push_back(nodeNbr);
      }

      std::vector<int> lastNodeNbr;
      lastNodeNbr.push_back(traffic_requests[t].middlebox_sequence.size());
      nbr[t].push_back(lastNodeNbr);
    }

    //////////CPLEX Variable//////////
    // gtnp = 1, if n-th node of traffic t is a middlebox of type p
    IloInt3dArray gtnp(env, kTrafficCount);
    for (int t = 0; t < kTrafficCount; ++t) {
      gtnp[t] = IloInt2dArray(env, trafficNodeCount[t]);
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        gtnp[t][n] = IloIntArray(env, kMboxTypes, 0, 1);
        for (int p = 0; p < kMboxTypes; ++p) {
          gtnp[t][n][p] = (trafficNodeType[t][n] == p);
        }
      }
    }
    // print_IloInt3dArray(gtnp, kTrafficCount, 5, kMboxTypes, "gtnp");

    //////////CPLEX Variable//////////
    // bar_xtnm = 1, if currently n-th node of traffic t is passing through m
    IloInt3dArray hat_xtnm(env, kTrafficCount);
    for (int t = 0; t < kTrafficCount; ++t) {
      hat_xtnm[t] = IloInt2dArray(env, trafficNodeCount[t]);
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        hat_xtnm[t][n] = IloIntArray(env, kMboxCount, 0, 1);
        for (int m = 0; m < kMboxCount; ++m) {
          hat_xtnm[t][n][m] = 0;
        }
      }
    }
    // TODO: read the previous configuration from file

    //^^^^^CPLEX Decision Variable^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // DECISION VAR: xtnm = 1, if n-th node of traffic t will pass through m
    IloIntVar3dArray xtnm(env, kTrafficCount);
    for (int t = 0; t < kTrafficCount; ++t) {
      xtnm[t] = IloIntVar2dArray(env, trafficNodeCount[t]);
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        xtnm[t][n] = IloIntVarArray(env, kMboxCount, 0, 1);
      }
    }
    //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    //////////CPLEX Variable//////////
    // ztn_n = 1, if node n of traffic t is provisioned on phy. node _n
    IloIntVar3dArray ztn_n(env, kTrafficCount);
    for (int t = 0; t < kTrafficCount; ++t) {
      ztn_n[t] = IloIntVar2dArray(env, trafficNodeCount[t]);
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        ztn_n[t][n] = IloIntVarArray(env, kSwitchCount, 0, 1);
      }
    }

    // DECISION VAR: ftl_l = R+, amount of bandwidth allocate to link l of
    // traffic t on phy. link _l
    // IloIntVar3dArray ftl_l(env, kTrafficCount);
    // for (int t = 0; t < kTrafficCount; ++t) {
    //  ftl_l[t] = IloIntVar2dArray(env, trafficLinkCount[t]);
    //  for (int l = 0; l < trafficLinkCount[t]; ++l) {
    //    ftl_l[t][l] = IloIntVarArray(env, kLinkCount, 0, 1000);
    //  }
    //}

    // DECISION VAR: htl_l = 1, if link l of traffic t provisioned on  phy. link
    // _l
    // this varibale is useful for expressing the SLA violation penalty
    // IloIntVar3dArray htl_l(env, kTrafficCount);
    // for (int t = 0; t < kTrafficCount; ++t) {
    //  htl_l[t] = IloIntVar2dArray(env, trafficLinkCount[t]);
    //  for (int l = 0; l < trafficLinkCount[t]; ++l) {
    //    htl_l[t][l] = IloIntVarArray(env, kLinkCount, 0, 1);
    //  }
    //}

    // print_IloInt2dArray(amp, kMboxCount, kMboxTypes, "amp");
    // print_IloInt3dArray(gtnp, kTrafficCount, 5, kMboxTypes, "gtnp");

    // ADD: constraint from htl_l
    // for (int t = 0; t < kTrafficCount; ++t) {
    //  for(int l = 0; l < trafficLinkCount[t]; ++l) {
    //    for(int _l = 0; _l < kLinkCount; ++_l) {
    //      model.add(IloIfThen(env, (ftl_l[t][l][_l] > 0), (htl_l[t][l][_l] ==
    // 1)));
    //      model.add(IloIfThen(env, (htl_l[t][l][_l] == 1), (ftl_l[t][l][_l] >
    // 0)));
    //    }
    //  }
    //}

    //-----CPLEX Constraint------------------------------------------------
    // ADD: constraint fot ym
    for (int m = 0; m < kMboxCount; ++m) {
      IloIntExpr sum(env);
      for (int t = 0; t < kTrafficCount; ++t) {
        for (int n = 0; n < trafficNodeCount[t]; ++n) {
          sum += xtnm[t][n][m];
        }
      }
      model.add(IloIfThen(env, sum > 0, ym[m] == 1));
      model.add(IloIfThen(env, ym[m] == 1, sum > 0));
    }
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: constraint for ztn_n
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        for (int m = 0; m < kMboxCount; ++m) {
          for (int _s = 0; _s < kSwitchCount; ++_s) {
            if (switch4mbox[m] == _s) {
              model.add(
                  IloIfThen(env, xtnm[t][n][m] == 1, ztn_n[t][n][_s] == 1));
            }
          }
        }
      }
    }
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        for (int _s = 0; _s < kSwitchCount; ++_s) {
          IloIntExpr sum(env);
          for (int m : mbox4switch[_s]) {
            sum += xtnm[t][n][m];
          }
          // model.add(IloIfThen(env, sum == 0, ztn_n[t][n][_n] == 0));
          model.add(ztn_n[t][n][_s] <= sum);
        }
      }
    }
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: old traffic constraint
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        for (int m = 0; m < kMboxCount; ++m) {
          model.add(xtnm[t][n][m] >= hat_xtnm[t][n][m]);
          cnst.add(xtnm[t][n][m] >= hat_xtnm[t][n][m]);
        }
      }
    }
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: ingress & egress constraint
    for (int t = 0, u_t, v_t; t < kTrafficCount; ++t) {
      u_t = traffic_requests[t].source;
      v_t = traffic_requests[t].destination;
      for (int m = 0; m < kMboxCount; ++m) {
        model.add(IloIfThen(env, xtnm[t][0][m] == 1,
                            ym[m] * _z_s_n[u_t][server4mbox[m]] == 1));
        model.add(IloIfThen(env, xtnm[t][trafficNodeCount[t] - 1][m] == 1,
                            ym[m] * _z_s_n[v_t][server4mbox[m]] == 1));
      }
    }
    //---------------------------------------------------------------------

    //^^^^^CPLEX Decision Variable^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // wtuv_u_v = 1, if logical link (u, v) of traffic t uses physical link (_u,
    // _v)
    IloIntVar5dArray wtuv_u_v(env, kTrafficCount);
    for (int t = 0; t < kTrafficCount; ++t) {
      wtuv_u_v[t] = IloIntVar4dArray(env, trafficNodeCount[t]);
      for (int u = 0; u < trafficNodeCount[t]; ++u) {
        wtuv_u_v[t][u] = IloIntVar3dArray(env, trafficNodeCount[t]);
        for (int v = 0; v < trafficNodeCount[t]; ++v) {
          wtuv_u_v[t][u][v] = IloIntVar2dArray(env, kSwitchCount);
          for (int _u = 0; _u < kSwitchCount; ++_u) {
            wtuv_u_v[t][u][v][_u] = IloIntVarArray(env, kSwitchCount, 0, 1);
          }
        }
      }
    }
    //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    //-----CPLEX Constraint------------------------------------------------
    // ADD: physical link capacity constraint
    for (int _u = 0; _u < kSwitchCount; ++_u) {
      for (int _v : __nbr[_u]) {
        if (_u < _v) {
          IloIntExpr sum(env);
          for (int t = 0, beta_t; t < kTrafficCount; ++t) {
            beta_t = traffic_requests[t].min_bandwidth;
            for (int n1 = 0; n1 < trafficNodeCount[t]; ++n1) {
              for (int n2 : nbr[t][n1]) {
                if (n1 < n2) {
                  sum += (wtuv_u_v[t][n1][n2][_u][_v] +
                          wtuv_u_v[t][n1][n2][_v][_u]) *
                         beta_t;
                }
              }
            }
          }
          model.add(sum <= beta_u_v[_u][_v]);
        }
      }
    }
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: flow constraint
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n1 = 0; n1 < trafficNodeCount[t]; ++n1) {
        for (int n2 : nbr[t][n1]) {
          if (n1 < n2) {
            for (int _u = 0; _u < kSwitchCount; ++_u) {
              IloIntExpr sum(env);
              for (int _v : __nbr[_u]) {
                sum +=
                    wtuv_u_v[t][n1][n2][_u][_v] - wtuv_u_v[t][n1][n2][_v][_u];
              }
              model.add(sum == ztn_n[t][n1][_u] - ztn_n[t][n2][_u]);
            }
          }
        }
      }
    }
    /*
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n1 = 0; n1 < trafficNodeCount[t]; ++n1) {
        for (int n2 : nbr[t][n1]) {
          if (n1 < n2) {
            IloIntExpr sum(env);
            for (int _u = 0; _u < kSwitchCount; ++_u) {
              for (int _v : __nbr[_u]) {
                sum += wtuv_u_v[t][n1][n2][_u][_v];
              }
            }
            model.add(sum > 0);
            for (int _s = 0; _s < kSwitchCount; ++_s) {
              //model.add(IloIfThen(env, ztn_n[t][n1][_s] != ztn_n[t][n2][_s],
    sum > 0));
              //model.add(IloIfThen(env, ztn_n[t][n1][_s] == ztn_n[t][n2][_s],
    wtuv_u_v[t][n1][n2][_s][_s] == 1));
            }
          }
        }
      }
    }
    */
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: middlebox processing capacity constraint
    // double beta_min = 0.0;
    for (int m = 0; m < kMboxCount; ++m) {
      IloExpr sum(env);
      for (int t = 0, beta_min; t < kTrafficCount; ++t) {
        beta_min = traffic_requests[t].min_bandwidth;
        for (int n = 0; n < trafficNodeCount[t]; ++n) {
          sum += xtnm[t][n][m] * beta_min;
        }
      }
      model.add(IloIfThen(env, (ym[m] == 1), (sum <= K_m[m])));
    }
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: every traffic should be embedded
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        IloExpr sum(env);
        for (int m = 0; m < kMboxCount; ++m) {
          sum += xtnm[t][n][m];
        }
        model.add(sum == 1);
        cnst.add(sum == 1);
      }
    }
    //---------------------------------------------------------------------

    //-----CPLEX Constraint------------------------------------------------
    // ADD: every middlebox in use should be embedded -- removed
    // it also includes the middlebox type constraint
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        for (int m = 0; m < kMboxCount; ++m) {
          // IloExpr sum_mbox(env);
          // for (int _n = 0; _n < kServerCount; ++_n) {
          //  sum_mbox += bm_n[m][_n];
          //}
          IloExpr sum_type(env);
          for (int p = 0; p < kMboxTypes; ++p) {
            sum_type += amp[m][p] * gtnp[t][n][p];
          }
          // model.add(xtnm[t][n][m] * sum_type * sum_mbox == 1);
          // model.add(IloIfThen(env, xtnm[t][n][m] == 1, sum_mbox == 1 &&
          // sum_type == 1));
          model.add(IloIfThen(env, xtnm[t][n][m] == 1, sum_type == 1));
          // cnst.add(IloIfThen(env, xtnm[t][n][m] == 1, sum_mbox == 1 &&
          // sum_type == 1));
        }
      }
    }
    //---------------------------------------------------------------------

    /*
    //ADD: a middlebox without traffic should not be deployed
    for (int _n = 0; _n < kServerCount; ++_n) {
      for (int m = 0; m < kMboxCount; ++m) {
        IloExpr sum(env);
        for (int t = 0; t < kTrafficCount; ++t) {
          for (int n = 0; n < trafficNodeCount[t]; ++n) {
            sum += xtnm[t][n][m];
          }
        }
        model.add(IloIfThen(env, sum == 0, bm_n[m][_n] == 0));
      }
    }
    */

    cout << "Done." << endl;

    ///////////////////////////////////////////////////
    //  Objective & Solution                         //
    ///////////////////////////////////////////////////

    IloNum alpha = 1.0;
    IloNum beta = 1.0;
    IloNum gamma = 1.0;
    IloNum lambda = 1.0;

    // build the objective
    IloExpr objective(env);
    // add deployment cost
    IloExpr deploymentCost(env);
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        for (int m = 0; m < kMboxCount; ++m) {
          deploymentCost += D_m[m] * (xtnm[t][n][m] - hat_xtnm[t][n][m]);
        }
      }
    }
    objective += alpha * deploymentCost;
    // add energy cost to the objective
    IloExpr energyCost(env);
    for (int _n = 0; _n < kServerCount; ++_n) {
      for (int r = 0; r < kResourceCount; ++r) {
        for (int i = 0, m; i < mbox4server[_n].size(); ++i) {
          m = mbox4server[_n][i];
          energyCost += ym[m] * cmr[m][r] * per_core_cost;
        }
      }
    }
    objective += beta * energyCost;
    // add traffic forearding cost
    IloExpr forwardingCost(env);
    for (int t = 0, beta; t < kTrafficCount; ++t) {
      beta = traffic_requests[t].min_bandwidth;
      for (int n1 = 0; n1 < trafficNodeCount[t]; ++n1) {
        for (int n2 : nbr[t][n1]) {
          if (n1 < n2) {
            for (int _u = 0; _u < kSwitchCount; ++_u) {
              for (int _v : __nbr[_u]) {
                forwardingCost +=
                    (wtuv_u_v[t][n1][n2][_u][_v] +
                     wtuv_u_v[t][n1][n2][_v][_u]) *
                    delta_u_v[_u][_v] * beta * per_bit_transit_cost;
              }
            }
          }
        }
      }
    }
    objective += gamma * forwardingCost;
    // add penalty for SLA vilations
    IloExpr penalty(env);
    for (int t = 0, beta; t < kTrafficCount; ++t) {
      traffic_request tr = traffic_requests[t];
      beta = tr.min_bandwidth;
      for (int n1 = 0; n1 < trafficNodeCount[t]; ++n1) {
        for (int n2 : nbr[t][n1]) {
          if (n1 < n2) {
            IloExpr delay(env);
            for (int _u = 0; _u < kSwitchCount; ++_u) {
              for (int _v : __nbr[_u]) {
                delay += wtuv_u_v[t][n1][n2][_u][_v] * delta_u_v[_u][_v] +
                         wtuv_u_v[t][n1][n2][_v][_u] * delta_u_v[_v][_u];
              }
            }
            penalty += (delay - tr.max_delay) * tr.delay_penalty;
          }
        }
      }
    }
    objective += lambda * penalty;

    // add the objective to the model
    model.add(objective >= 0);
    model.add(IloMinimize(env, objective));

    // solve the problem
    // IloCplex cplex(model);
    IloTimer timer(env);
    cout << endl << endl << "Invoking solver..." << endl;
    for (int i = 0; i < cnst.getSize(); ++i) {
      pref.add(1.0);
    }
    // cout << "cnst count: " << cnst_count << endl;
    timer.restart();
    if (!cplex.solve()) {
      timer.stop();
      cout << "Could not solve ILP!" << endl;
      cout << "Solution Status = " << cplex.getStatus() << endl;
      /*
      cout << "cnst size " << cnst.getSize() << endl;
      if(cplex.refineConflict(cnst, pref)) {
        cout << "refining" << endl;
        IloCplex::ConflictStatusArray conflict = cplex.getConflict(cnst);
        //env.getImpl()->useDetailedDisplay(IloTrue);
        cout << "Conflict :" << endl;
        for (IloInt i = 0; i<cnst.getSize(); i++) {
          if ( conflict[i] == IloCplex::ConflictMember)
               cout << "Proved  : " << cnst[i] << endl;
          if ( conflict[i] == IloCplex::ConflictPossibleMember)
               cout << "Possible: " << cnst[i] << endl;
        }
      }
      */
      throw(-1);
    }
    timer.stop();

    std::vector<int> result[kTrafficCount];
    double resultValue;

    cout << "Solution Status = " << cplex.getStatus() << endl;
    cout << "Solution Value = " << (resultValue = cplex.getObjValue()) << endl;

    // print xtnm
    cout << endl;
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        IloNumArray xtnm_vals(env, kMboxCount);
        cplex.getValues(xtnm[t][n], xtnm_vals);
        for (int m = 0; m < kMboxCount; ++m) {
          if (xtnm_vals[m] == 1) {
            result[t].push_back(pseudo2actual[switch4mbox[m]]);
            cout << "Traffic " << t << " node " << n
                 << " provisioned on middlebox " << m << endl;
          }
        }
      }
    }

    // print ym
    cout << endl;
    IloNumArray ym_vals(env, kMboxCount);
    cplex.getValues(ym, ym_vals);
  std:
    string type = "";
    for (int m = 0, sw; m < kMboxCount; ++m) {
      if (ym_vals[m] == 1) {
        if (mboxType[m] == 0) {
          type = "Ingress";
        } else if (mboxType[m] == 1) {
          type = "Egress";
        } else {
          type = middleboxes[mboxType[m] - 2].middlebox_name;
        }
        sw = (server4mbox[m] < kSwitchCount) ? server4mbox[m]
                                             : (server4mbox[m] - kSwitchCount);
        cout << "Middlebox " << m << " (" << type << ") is active on server "
             << sw << endl;
      }
    }

    // print ztn_n
    cout << endl;
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n = 0; n < trafficNodeCount[t]; ++n) {
        IloNumArray ztn_n_vals(env, kSwitchCount);
        cplex.getValues(ztn_n[t][n], ztn_n_vals);
        for (int _s = 0; _s < kSwitchCount; ++_s) {
          if (ztn_n_vals[_s] == 1) {
            // result[t].push_back(pseudo2actual[_s]);
            cout << "Traffic " << t << " node " << n
                 << " provisioned on switch " << pseudo2actual[_s]
                 << " pseudo-switch " << _s << endl;
          }
        }
      }
    }

    cout << endl;
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n1 = 0; n1 < trafficNodeCount[t]; ++n1) {
        for (int n2 : nbr[t][n1]) {
          if (n1 < n2) {
            for (int _u = 0; _u < kSwitchCount; ++_u) {
              // IloNumArray wtuv_u_v_vals(env, kSwitchCount);
              // cplex.getValues(wtuv_u_v[t][n1][n2][_u], wtuv_u_v_vals);
              IloNum value;
              for (int _v : __nbr[_u]) {
                value = cplex.getValue(wtuv_u_v[t][n1][n2][_u][_v]);
                if (value == 1) {
                  cout << "Traffic link (" << n1 << ", " << n2
                       << ") mapped to phy. link (" << _u << ", " << _v << ")"
                       << endl;
                }
              }
              // value = cplex.getValue(wtuv_u_v[t][n1][n2][_u][_u]);
              // if (value == 1) {
              //  cout << "Traffic link (" << n1 << ", " << n2 << ") mapped to
              // phy. link (" << _u << ", " << _u << ")" << endl;
              //}
            }
          }
        }
      }
    }

    // Final output
    cout << endl << "========================================" << endl
         << "Output:" << endl;
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int n : result[t]) {
        cout << n << " ";
      }
      cout << endl;
    }
    cout << resultValue << endl;
    cout << timer.getTime() << endl;
    cout << "========================================" << endl;

    /*
    //print ftl_l
    cout << endl;
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int l = 0; l < trafficLinkCount[t]; ++l) {
        IloNumArray ftl_l_vals(env, kLinkCount);
        cplex.getValues(ftl_l[t][l], ftl_l_vals);
        for (int _l = 0; _l < kLinkCount; ++_l) {
          if (ftl_l_vals[_l] > 0) {
            cout << "Traffic " << t << " link " << l << " bandwidth on phy. link
    " << _l << " " << ftl_l_vals[_l]  << endl;
          }
        }
      }
    }

    //print htl_l
    cout << endl;
    for (int t = 0; t < kTrafficCount; ++t) {
      for (int l = 0; l < trafficLinkCount[t]; ++l) {
        IloNumArray htl_l_vals(env, kLinkCount);
        cplex.getValues(htl_l[t][l], htl_l_vals);
        for (int _l = 0; _l < kLinkCount; ++_l) {
          if (htl_l_vals[_l] == 1) {
            cout << "Traffic " << t << " link " << l << " provisioned on phy.
    link " << _l << endl;
          }
        }
      }
    }
    */
  }
  /*
  catch (IloAlgorithm::CannotChangeException& e) {
    std::cerr << "CannotChangeException:" << e << std::endl;
    IloExtractableArray& es = e.getExtractables();

    for (IloInt i = 0; i < es.getSize(); ++i)
      std::cerr << "  " << i << ": " << es[i] << std::endl;

    throw;
  }
  catch (IloAlgorithm::CannotExtractException& e) {
    std::cerr << "CannotExtractException:" << e << std::endl;
    IloExtractableArray& es = e.getExtractables();

    for (IloInt i = 0; i < es.getSize(); ++i)
      std::cerr << "  " << i << ": " << es[i] << std::endl;

    throw;
  }
  */
  catch (IloException & e) {
    cerr << "Concert exception caught: " << e << endl;
  }
  catch (...) {
    cerr << "Unknown exception caught" << endl;
  }

  env.end();
}

#endif // MIDDLEBOX_PLACEMENT_SRC_CPLEX_H
