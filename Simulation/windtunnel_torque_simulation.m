% Wind tunnel: torque-domain PID + feedforward + approximate V20 slip model
% Author: R.E. van Eekelen
% Run in MATLAB: windtunnel_torque_simulation
% Assumed motor model: 4 poles, 50 Hz, 1450 rpm, 4 kW nameplate.
% All curves are SIMULATED and MUST NOT be used for commissioning.
clear; close all; clc;
Ts=0.005; t=(0:Ts:120)';
Jm=0.10; Jfan=0.20; J=Jm+Jfan; % EXAMPLE ONLY: replace with measured inertias
n0=1450; w0=2*pi*n0/60; v0=40; Kv=v0/w0;
p=2; Pn=4000; fn=50;
Tnom=Pn/w0;
Ks=Tnom/(2*pi*fn/p-w0);
Kload=Tnom/w0^2;
fd=0.02; Ad=5; emax=0.5; wd=2*pi*fd;
M=Ad/emax; Kp=M*J*wd/Kv; wb=Kp*Kv/J;
Ti=10/wb; Td=3/wb; Tf=1/(10*wb);
Ki=Kp/Ti; Kd=Kp*Td;
fprintf('J=%.4f, Ks=%.4f, Kp=%.4f, Ki=%.4f, Kd=%.4f, Ti=%.4f, Td=%.4f, Tf=%.4f\n',...
 J,Ks,Kp,Ki,Kd,Ti,Td,Tf);
% Quintic reference: 0 -> 20 m/s, start at t=5, nominal duration=12 s.
t0=5; dur=12; target=20;
q=max(0,min(1,(t-t0)/dur));
vref=target*(10*q.^3-15*q.^4+6*q.^5);
vdot=zeros(size(t));
active=(t>=t0 & t<t0+dur);
vdot(active)=target*(30*q(active).^2-60*q(active).^3+30*q(active).^4)/dur;
% Compare PID alone with PID + torque feedforward.
Y=zeros(numel(t),2); F=zeros(numel(t),2); Torque=zeros(numel(t),2);
for mode=1:2
 omega=0; integral=0; oldError=0; oldMeasurement=0; dterm=0;
 for k=1:numel(t)
   v=Kv*omega;
   % Additive OUTPUT disturbance - for illustration begins at t=45.
   disturbance=0;
   if t(k)>=45, disturbance=2*sin(2*pi*fd*(t(k)-45)); end
   sensed=v+disturbance;
   error=vref(k)-sensed;
   % Tustin integration and filtered derivative-on-measurement.
   deltaI=Ki*Ts*(error+oldError)/2;
   dterm=((2*Tf-Ts)/(2*Tf+Ts))*dterm...
     -(2*Kd/(2*Tf+Ts))*(sensed-oldMeasurement);
   trial=Kp*error+integral+deltaI+dterm;
   % Anti-windup based on total actuator torque with +/-40 Nm correction.
   if abs(trial)<=40 || sign(deltaI)~=sign(trial)
      integral=integral+deltaI;
   end
   tpid=max(-40,min(40,Kp*error+integral+dterm));
   tff=(mode==2)*J*vdot(k)/Kv;
   tload=Kload*omega*abs(omega);
   tdemand=tload+tff+tpid;
   % Model inversion: electro-mechanical slip estimate, no VFD dynamics.
   f=max(0,min(50,(omega+tdemand/Ks)*p/(2*pi)));
   tmotor=Ks*(2*pi*f/p-omega);
   omega=max(0,omega+Ts*(tmotor-tload)/J);
   Y(k,mode)=sensed; F(k,mode)=f; Torque(k,mode)=tmotor;
   oldError=error; oldMeasurement=sensed;
 end
end
figure('Name','Wind tunnel reference tracking');
plot(t,vref,'k--',t,Y(:,1),t,Y(:,2));grid on;
xlabel('Time [s]');ylabel('Airspeed [m/s]');
legend('Reference','PID only','PID + feedforward','Location','best');
figure('Name','V20 frequency command (model only)');
plot(t,F);grid on;xlabel('Time [s]');ylabel('Command [Hz]');
legend('PID only','PID + feedforward','Location','best');
figure('Name','Motor torque (estimated)');
plot(t,Torque);grid on;xlabel('Time [s]');ylabel('Motor torque [Nm]');
legend('PID only','PID + feedforward','Location','best');
fprintf('NOTE: slip slope estimated from one rated point; no real VFD ramp, slip compensation or fan curve.\n');
