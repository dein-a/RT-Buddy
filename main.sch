{
  "version": 1,
  "symbols": [
    {
      "id": "esp32_1",
      "type": "reg:esp32-devkit-v1",
      "ref": "U1",
      "x": 37,
      "y": 45,
      "rotation": 0,
      "mirror": false
    },
    {
      "id": "tft_1",
      "type": "reg:ili9341-tft-2-8",
      "ref": "U2",
      "x": 87,
      "y": 62,
      "rotation": 0,
      "mirror": false
    },
    {
      "id": "r_touch_top",
      "type": "resistor",
      "ref": "R1",
      "x": 120,
      "y": 74,
      "rotation": 0,
      "value": "1k",
      "mirror": false
    },
    {
      "id": "r_touch_bot",
      "type": "resistor",
      "ref": "R2",
      "x": 87,
      "y": 23,
      "rotation": 0,
      "value": "2k",
      "mirror": false
    }
  ],
  "wires": [
    {
      "id": "bbs_w_0",
      "points": [
        {
          "x": 43,
          "y": 33
        },
        {
          "x": 90,
          "y": 33
        },
        {
          "x": 90,
          "y": 28
        },
        {
          "x": 87,
          "y": 28
        },
        {
          "x": 87,
          "y": 26
        }
      ],
      "net": "0"
    },
    {
      "id": "bbs_w_1",
      "points": [
        {
          "x": 87,
          "y": 26
        },
        {
          "x": 87,
          "y": 28
        },
        {
          "x": 79,
          "y": 28
        },
        {
          "x": 79,
          "y": 17
        },
        {
          "x": 47,
          "y": 17
        },
        {
          "x": 47,
          "y": 33
        },
        {
          "x": 31,
          "y": 33
        }
      ],
      "net": "0"
    },
    {
      "id": "bbs_w_2",
      "points": [
        {
          "x": 31,
          "y": 49
        },
        {
          "x": 44,
          "y": 49
        },
        {
          "x": 44,
          "y": 63
        },
        {
          "x": 120,
          "y": 63
        },
        {
          "x": 120,
          "y": 77
        }
      ],
      "net": "N_IO32"
    },
    {
      "id": "bbs_w_3",
      "points": [
        {
          "x": 31,
          "y": 49
        },
        {
          "x": 44,
          "y": 49
        },
        {
          "x": 44,
          "y": 12
        },
        {
          "x": 87,
          "y": 12
        },
        {
          "x": 87,
          "y": 20
        }
      ],
      "net": "N_IO32"
    }
  ],
  "junctions": [
    {
      "id": "j_43_33",
      "x": 43,
      "y": 33
    },
    {
      "id": "j_87_28",
      "x": 87,
      "y": 28
    },
    {
      "id": "j_47_33",
      "x": 47,
      "y": 33
    },
    {
      "id": "j_44_49",
      "x": 44,
      "y": 49
    }
  ],
  "netLabels": [
    {
      "id": "bbs_nl_0",
      "name": "GND",
      "x": 43,
      "y": 33,
      "owner": "esp32_1",
      "anchor": "start"
    },
    {
      "id": "bbs_nl_1",
      "name": "N_IO32",
      "x": 31,
      "y": 49,
      "owner": "esp32_1",
      "anchor": "end"
    }
  ],
  "probes": [],
  "directives": [],
  "breadboardSync": {
    "sourceHash": "15421:-1096399250",
    "symbolIds": [
      "esp32_1",
      "tft_1",
      "r_touch_top",
      "r_touch_bot"
    ],
    "wireIds": [
      "bbs_w_0",
      "bbs_w_1",
      "bbs_w_2",
      "bbs_w_3"
    ],
    "netLabelIds": [
      "bbs_nl_0",
      "bbs_nl_1"
    ]
  }
}