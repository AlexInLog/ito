window.BENCHMARK_DATA = {
  "lastUpdate": 1784617804887,
  "repoUrl": "https://github.com/AlexInLog/ito",
  "entries": {
    "Ito benchmarks - Ubuntu - clang": [
      {
        "commit": {
          "author": {
            "email": "32845901+AlexInLog@users.noreply.github.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "7b0a3b9be8b8eb9b82fd8111f469cd77df50b95b",
          "message": "Update build.yml",
          "timestamp": "2026-07-16T23:55:59+03:00",
          "tree_id": "13e2f1a3f6f6db5d95ec134f3c8fafe2d5253a6d",
          "url": "https://github.com/AlexInLog/ito/commit/7b0a3b9be8b8eb9b82fd8111f469cd77df50b95b"
        },
        "date": 1784235466397,
        "tool": "catch2",
        "benches": [
          {
            "name": "call no-op coro",
            "value": 19.6662,
            "range": "± 0.892856",
            "unit": "ns",
            "extra": "100 samples\n1544 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "32845901+AlexInLog@users.noreply.github.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "committer": {
            "email": "victimsnino@gmail.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "distinct": true,
          "id": "55f3999b07924cf28a98d8179a6a5ce4fc5037f3",
          "message": "Update build.yml",
          "timestamp": "2026-07-17T00:08:00+03:00",
          "tree_id": "45209177800e7d396d0b72cee3368f44d9a70c7b",
          "url": "https://github.com/AlexInLog/ito/commit/55f3999b07924cf28a98d8179a6a5ce4fc5037f3"
        },
        "date": 1784236162718,
        "tool": "catch2",
        "benches": [
          {
            "name": "call no-op coro",
            "value": 20.1396,
            "range": "± 2.30152",
            "unit": "ns",
            "extra": "100 samples\n1526 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "32845901+AlexInLog@users.noreply.github.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "5a4af2261f7a20e19ab6842b22127a93f7b2268c",
          "message": "Extend bench (#16)\n\n* Update build.yml\n\n* extend bench",
          "timestamp": "2026-07-17T00:16:00+03:00",
          "tree_id": "f9ba00dd90794f34b1dcf669d8dc716b18d02005",
          "url": "https://github.com/AlexInLog/ito/commit/5a4af2261f7a20e19ab6842b22127a93f7b2268c"
        },
        "date": 1784236628750,
        "tool": "catch2",
        "benches": [
          {
            "name": "call no-op coro",
            "value": 19.3319,
            "range": "± 0.981979",
            "unit": "ns",
            "extra": "100 samples\n1464 iterations"
          },
          {
            "name": "call no-op coro as child",
            "value": 35.9409,
            "range": "± 2.0125",
            "unit": "ns",
            "extra": "100 samples\n815 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "32845901+AlexInLog@users.noreply.github.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "98c9d6b89d85bef09c644832919639e858dc8568",
          "message": "Call soon (#17)\n\n* init\n\n* extend tests\n\n* fix\n\n* new benchmarks\n\n* extended logic with bench\n\n* revert back\n\n* fix\n\n* fixes\n\n* fixes\n\n* simplify",
          "timestamp": "2026-07-18T00:18:03+03:00",
          "tree_id": "0da73d600ba7d72c4e770a8c79c01e80219a62de",
          "url": "https://github.com/AlexInLog/ito/commit/98c9d6b89d85bef09c644832919639e858dc8568"
        },
        "date": 1784323156395,
        "tool": "catch2",
        "benches": [
          {
            "name": "unuqie_ptr",
            "value": 14.0506,
            "range": "± 0.643739",
            "unit": "ns",
            "extra": "100 samples\n2203 iterations"
          },
          {
            "name": "small std::function",
            "value": 1.75602,
            "range": "± 0.0735071",
            "unit": "ns",
            "extra": "100 samples\n17636 iterations"
          },
          {
            "name": "big std::function",
            "value": 19.6661,
            "range": "± 0.832528",
            "unit": "ns",
            "extra": "100 samples\n1573 iterations"
          },
          {
            "name": "creation of coro",
            "value": 14.7876,
            "range": "± 0.933918",
            "unit": "ns",
            "extra": "100 samples\n2098 iterations"
          },
          {
            "name": "call no-op coro",
            "value": 21.7797,
            "range": "± 1.02759",
            "unit": "ns",
            "extra": "100 samples\n1422 iterations"
          },
          {
            "name": "call_soon before no-op coro",
            "value": 34.4271,
            "range": "± 1.74121",
            "unit": "ns",
            "extra": "100 samples\n907 iterations"
          },
          {
            "name": "call no-op coro as child",
            "value": 37.9725,
            "range": "± 1.80834",
            "unit": "ns",
            "extra": "100 samples\n787 iterations"
          },
          {
            "name": "call coro calling function",
            "value": 22.233,
            "range": "± 1.14583",
            "unit": "ns",
            "extra": "100 samples\n1441 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "32845901+AlexInLog@users.noreply.github.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "cd2169f7a1aefe0a545547f8d00233f9fcb013ea",
          "message": "refactor value handler (#18)\n\n* refactor value\n\n* hide as private\n\n* fix is_ready\n\n* rename",
          "timestamp": "2026-07-18T16:17:18+03:00",
          "tree_id": "bf46512fec5294c0defab3da30cd3366a0435efb",
          "url": "https://github.com/AlexInLog/ito/commit/cd2169f7a1aefe0a545547f8d00233f9fcb013ea"
        },
        "date": 1784380712103,
        "tool": "catch2",
        "benches": [
          {
            "name": "unuqie_ptr",
            "value": 14.202,
            "range": "± 0.895991",
            "unit": "ns",
            "extra": "100 samples\n2108 iterations"
          },
          {
            "name": "small std::function",
            "value": 2.17888,
            "range": "± 0.11108",
            "unit": "ns",
            "extra": "100 samples\n13310 iterations"
          },
          {
            "name": "big std::function",
            "value": 20.0845,
            "range": "± 2.34973",
            "unit": "ns",
            "extra": "100 samples\n1550 iterations"
          },
          {
            "name": "creation of coro",
            "value": 15.482,
            "range": "± 2.25096",
            "unit": "ns",
            "extra": "100 samples\n2068 iterations"
          },
          {
            "name": "call no-op coro",
            "value": 23.4155,
            "range": "± 3.17847",
            "unit": "ns",
            "extra": "100 samples\n1330 iterations"
          },
          {
            "name": "call_soon before no-op coro",
            "value": 36.2625,
            "range": "± 3.31527",
            "unit": "ns",
            "extra": "100 samples\n857 iterations"
          },
          {
            "name": "call no-op coro as child",
            "value": 41.3552,
            "range": "± 4.38689",
            "unit": "ns",
            "extra": "100 samples\n741 iterations"
          },
          {
            "name": "call coro calling function",
            "value": 24.4719,
            "range": "± 3.03908",
            "unit": "ns",
            "extra": "100 samples\n1303 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "32845901+AlexInLog@users.noreply.github.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "b5101b3e8201a791f0f7c7fc1003ec2f548c4f3c",
          "message": "async futures (#19)\n\n* add async futures\n\n* readme\n\n* readme\n\n* fix inheritance\n\n* fix UB\n\n* lint\n\n* betteer checks",
          "timestamp": "2026-07-18T18:38:09+03:00",
          "tree_id": "09d539d74f56df97a8149f95fcadc425847d2c82",
          "url": "https://github.com/AlexInLog/ito/commit/b5101b3e8201a791f0f7c7fc1003ec2f548c4f3c"
        },
        "date": 1784389168502,
        "tool": "catch2",
        "benches": [
          {
            "name": "creation of coro",
            "value": 14.7655,
            "range": "± 0.743683",
            "unit": "ns",
            "extra": "100 samples\n2100 iterations"
          },
          {
            "name": "call no-op coro",
            "value": 20.9121,
            "range": "± 1.4799",
            "unit": "ns",
            "extra": "100 samples\n1495 iterations"
          },
          {
            "name": "call_soon before no-op coro",
            "value": 32.7712,
            "range": "± 1.77303",
            "unit": "ns",
            "extra": "100 samples\n939 iterations"
          },
          {
            "name": "call no-op coro as child",
            "value": 38.2644,
            "range": "± 1.64556",
            "unit": "ns",
            "extra": "100 samples\n802 iterations"
          },
          {
            "name": "call coro calling function",
            "value": 22.1458,
            "range": "± 1.12422",
            "unit": "ns",
            "extra": "100 samples\n1397 iterations"
          },
          {
            "name": "resolve future before await",
            "value": 26.3235,
            "range": "± 1.45023",
            "unit": "ns",
            "extra": "100 samples\n1130 iterations"
          },
          {
            "name": "resolve future inside signal",
            "value": 55.8034,
            "range": "± 2.57204",
            "unit": "ns",
            "extra": "100 samples\n562 iterations"
          },
          {
            "name": "unuqie_ptr",
            "value": 21.5342,
            "range": "± 1.31033",
            "unit": "ns",
            "extra": "100 samples\n1472 iterations"
          },
          {
            "name": "small std::function",
            "value": 1.84048,
            "range": "± 0.887569",
            "unit": "ns",
            "extra": "100 samples\n17631 iterations"
          },
          {
            "name": "big std::function",
            "value": 24.5657,
            "range": "± 1.12158",
            "unit": "ns",
            "extra": "100 samples\n1263 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "32845901+AlexInLog@users.noreply.github.com",
            "name": "Aleksey Loginov",
            "username": "AlexInLog"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "58442ec92bc48003f4815349a000a3557acd98e9",
          "message": "Minor reorg (#20)\n\n* make reorg\n\n* MD\n\n* revert",
          "timestamp": "2026-07-21T10:07:32+03:00",
          "tree_id": "3e1735d3bf1b418b32340376caf954a0c8a35016",
          "url": "https://github.com/AlexInLog/ito/commit/58442ec92bc48003f4815349a000a3557acd98e9"
        },
        "date": 1784617804283,
        "tool": "catch2",
        "benches": [
          {
            "name": "unuqie_ptr",
            "value": 14.7643,
            "range": "± 1.91392",
            "unit": "ns",
            "extra": "100 samples\n2134 iterations"
          },
          {
            "name": "small std::function",
            "value": 2.21981,
            "range": "± 0.372246",
            "unit": "ns",
            "extra": "100 samples\n13423 iterations"
          },
          {
            "name": "big std::function",
            "value": 21.2445,
            "range": "± 3.52405",
            "unit": "ns",
            "extra": "100 samples\n1561 iterations"
          },
          {
            "name": "resolve future before await",
            "value": 30.3063,
            "range": "± 6.20999",
            "unit": "ns",
            "extra": "100 samples\n1143 iterations"
          },
          {
            "name": "resolve future inside signal",
            "value": 47.374,
            "range": "± 6.73218",
            "unit": "ns",
            "extra": "100 samples\n667 iterations"
          },
          {
            "name": "creation of coro",
            "value": 15.9835,
            "range": "± 2.24193",
            "unit": "ns",
            "extra": "100 samples\n2041 iterations"
          },
          {
            "name": "call no-op coro",
            "value": 22.9849,
            "range": "± 4.42542",
            "unit": "ns",
            "extra": "100 samples\n1466 iterations"
          },
          {
            "name": "call_soon before no-op coro",
            "value": 36.2281,
            "range": "± 6.34677",
            "unit": "ns",
            "extra": "100 samples\n860 iterations"
          },
          {
            "name": "call no-op coro as child",
            "value": 45.3442,
            "range": "± 9.82066",
            "unit": "ns",
            "extra": "100 samples\n780 iterations"
          },
          {
            "name": "call coro calling function",
            "value": 24.7088,
            "range": "± 4.35278",
            "unit": "ns",
            "extra": "100 samples\n1380 iterations"
          }
        ]
      }
    ]
  }
}