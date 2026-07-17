window.BENCHMARK_DATA = {
  "lastUpdate": 1784323156992,
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
      }
    ]
  }
}