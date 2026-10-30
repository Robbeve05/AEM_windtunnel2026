%% Wind tunnel: torque PID + feedforward, induction motor frequency proxy
% R.E. van Eekelen - simulation ONLY; no V20 hardware control.
% Requires Control System Toolbox for tf, feedback, margin.
% All catalogue data are conditional on verified exact motor variant.
clearvars; close all; clc
Ts = 0.005;
P = 4000; nRated = 1450; fRated = 50; polePairs = 2;
wRated = 2*pi*nRated/60;
Tnom = P/wRated;
wSync = 2*pi*fRated/polePairs;
Ks = Tnom/(wSync-wRated); % approximate SECANT, not identified slope
JmotorCatalogue = 0.0182; % W21 IE3 4-kW 4-pole 112M ONLY
motorVariantConfirmed = false;
Jfan = NaN; % insert measured/estimated fan inertia after verification
fanInertiaConfirmed = false;
if ~motorVariantConfirmed || ~fanInertiaConfirmed || ~isfinite(Jfan) || Jfan<=0
    warning('Missing verified motor variant / fan inertia. Demonstration only.');
    J = 0.30; % EXAMPLE, NOT the physical wind tunnel
else
    J = JmotorCatalogue + Jfan;
end
vNom = 40; % measured approximately; pairing with 1450 rpm NOT verified
Kv = vNom/wRated;
fd = 0.02; Ad = 5; maxErr = 0.5;
wd = 2*pi*fd; M = Ad/maxErr;
Kp = M*J*wd/Kv; wb = Kp*Kv/J;
Ti=10/wb; Td=3/wb; Tf=1/(10*wb);
Ki=Kp/Ti; Kd=Kp*Td;
s=tf('s'); G=Kv/(J*s);
C=Kp + Ki/s + Kd*s/(Tf*s+1);
L=C*G; S=feedback(1,L); H=feedback(C*G,1);
[~,pm]=margin(L);
fprintf('J=%.5g kg m2, Ks≈%.5g Nm/(rad/s) [unverified]\n',J,Ks);
fprintf('Kp %.5g, Ki %.5g, Kd %.5g, Ti %.5g, Td %.5g, Tf %.5g; PM %.2f deg\n',Kp,Ki,Kd,Ti,Td,Tf,pm);
fprintf('Sinusoidal output-disturbance residual at %.3g Hz = %.4g m/s\n',fd, Ad*abs(freqresp(S,wd)));
figure; margin(L); grid on; title('Torque-controlled open-loop design');
figure; step(20*H,120); grid on; title('Ideal torque PID: 20 m/s step');
% Torque feedforward: Tff=(J/Kv) dv_ref/dt.
% Torque demand: Trequest=Tff + Tpid + b*omega*abs(omega).
% Approximate inversion (requires rotor speed, motor load and calibrated Ks):
% f=(omega + Trequest/Ks)*polePairs/(2*pi).
% The estimate is valid only locally and excludes V20 ramp and drive dynamics.
% For actual V20 operation identify frequency->airspeed G(s), add command ramp,
% rate/saturation limits, combined actuator antiwindup and safety interlocks.
