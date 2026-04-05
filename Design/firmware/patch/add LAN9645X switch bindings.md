Add bindings for LAN9645X switch. We use a fallback compatible for the
smallest SKU microchip,lan96455s-switch.

Reviewed-by: Steen Hegelund <Steen.Hegelund@xxxxxxxxxxxxx>
Signed-off-by: Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
---
 .../net/dsa/microchip,lan9645x-switch.yaml         | 137 +++++++++++++++++++++
 MAINTAINERS                                        |   1 +
 2 files changed, 138 insertions(+)

diff --git a/Documentation/devicetree/bindings/net/dsa/microchip,lan9645x-switch.yaml b/Documentation/devicetree/bindings/net/dsa/microchip,lan9645x-switch.yaml
new file mode 100644
index 000000000000..4a19dfa7e9d5
--- /dev/null
+++ b/Documentation/devicetree/bindings/net/dsa/microchip,lan9645x-switch.yaml
@@ -0,0 +1,137 @@
+# SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause
+%YAML 1.2
+---
+$id: http://devicetree.org/schemas/net/dsa/microchip,lan9645x-switch.yaml#
+$schema: http://devicetree.org/meta-schemas/core.yaml#
+
+title: Microchip LAN9645x Ethernet switch
+
+maintainers:
+  - Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
+
+description: |
+  The LAN9645x switch is a multi-port Gigabit AVB/TSN Ethernet switch with
+  five integrated 10/100/1000Base-T PHYs. In addition to the integrated PHYs,
+  it supports up to 2 RGMII/RMII, up to 2 BASE-X/SERDES/2.5GBASE-X and one
+  Quad-SGMII interfaces.
+
+properties:
+  compatible:
+    oneOf:
+      - enum:
+          - microchip,lan96455s-switch
+      - items:
+          - enum:
+              - microchip,lan96455f-switch
+              - microchip,lan96457f-switch
+              - microchip,lan96459f-switch
+              - microchip,lan96457s-switch
+              - microchip,lan96459s-switch
+          - const: microchip,lan96455s-switch
+
+  reg:
+    maxItems: 1
+
+$ref: dsa.yaml#
+
+patternProperties:
+  "^(ethernet-)?ports$":
+    type: object
+    additionalProperties: true
+    patternProperties:
+      "^(ethernet-)?port@[0-8]$":
+        type: object
+        description: Ethernet switch ports
+
+        $ref: dsa-port.yaml#
+
+        properties:
+          microchip,led-drive-mode:
+            $ref: /schemas/types.yaml#/definitions/uint32
+            description: |
+              Set the LED drive mode for the copper PHY associated with
+              this port.
+
+                0 - LED1 and LED2 in open-drain mode
+                1 - LED1 in active drive mode (can be used for single-LED
+                    configurations requiring active drive)
+                2 - Reserved
+                3 - LED1 and LED2 in active drive mode
+            minimum: 0
+            maximum: 3
+
+        unevaluatedProperties: false
+
+oneOf:
+  - required:
+      - ports
+  - required:
+      - ethernet-ports
+
+required:
+  - compatible
+  - reg
+
+unevaluatedProperties: false
+
+examples:
+  - |
+    soc {
+        #address-cells = <1>;
+        #size-cells = <0>;
+
+      ethernet-switch@0 {
+        reg = <0>;
+        compatible = "microchip,lan96459f-switch", "microchip,lan96455s-switch";
+        pinctrl-0 = <&lan9645x_leds>;
+        pinctrl-names = "default";
+
+        ethernet-ports {
+          #address-cells = <1>;
+          #size-cells = <0>;
+
+          port@0 {
+            reg = <0>;
+            label = "lan0";
+            phy-mode = "gmii";
+            phy-handle = <&cuphy0>;
+          };
+
+          port@1 {
+            reg = <1>;
+            label = "lan1";
+            phy-mode = "gmii";
+            phy-handle = <&cuphy1>;
+            microchip,led-drive-mode = <3>;
+          };
+
+          port@2 {
+            reg = <2>;
+            label = "lan2";
+            phy-mode = "gmii";
+            phy-handle = <&cuphy2>;
+          };
+
+          port@3 {
+            reg = <3>;
+            label = "lan3";
+            phy-mode = "gmii";
+            phy-handle = <&cuphy3>;
+          };
+
+          port@7 {
+            reg = <7>;
+            label = "lan7";
+            phy-mode = "rgmii-id";
+            ethernet = <&cpu_host_port>;
+            fixed-link {
+              speed = <1000>;
+              full-duplex;
+              pause;
+            };
+          };
+        };
+      };
+    };
+
+...
diff --git a/MAINTAINERS b/MAINTAINERS
index 2712aaf7cedd..ab92b342877b 100644
--- a/MAINTAINERS
+++ b/MAINTAINERS
@@ -17291,6 +17291,7 @@ M:	Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
 M:	UNGLinuxDriver@xxxxxxxxxxxxx
 L:	netdev@xxxxxxxxxxxxxxx
 S:	Maintained
+F:	Documentation/devicetree/bindings/net/dsa/microchip,lan9645x-switch.yaml
 F:	include/linux/dsa/lan9645x.h
 F:	net/dsa/tag_lan9645x.c
 

-- 
2.52.0

